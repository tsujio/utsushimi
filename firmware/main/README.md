# ファームウェア

## 実装範囲

黒背景に丸い白目と丸い黒い瞳を左右に表示し、ランダムに瞬きします。瞳を少し内向きに置いた、かわいらしい記号的な表情です。Step 1.1の表示、Step 1.2〜1.4の身体反応モデル（乾燥・快不快・欲求・瞬き・表情）を実装しています。視線アニメーション・実電極のタッチ検出は未実装で、Step 1全体の完了を意味しません。

- `app_main.cpp`：起動・メモリ情報ログ、表情状態の更新、独立した表示タスク。
- `behavior/body_behavior.*`：`BodyState`、`Stimulus`、欲求・確率判定・表情の関数と、それらを順に進める `BodyBehavior`。ESP-IDF・描画に依存しません。
- `behavior/blink.*`：選択済みの単発・二連を時刻から描く `Action`。乱数や身体状態の管理を含みません。
- `board/amoled.c`：確認済みRev2.0のI2C、TCA9554のEXIO0リセット、QSPI、DMA転送。
- `display/eye_renderer.*`：ハードウェアに依存しないC++描画。`EyeState`で視線・開き具合・瞳の大きさを指定。

UI部品は不要なので、LVGLを導入せずRGB565を直接生成しています。画像の切り替えではなくパラメータで描画します。

## Makefileでのビルドと書き込み

リポジトリのルートから実行します。ESP-IDF v5.5.6を使用し、既定では `~/esp/esp-idf/export.sh` を自動的に読み込みます。

```bash
make build
make install PORT=/dev/ttyACM0
make monitor PORT=/dev/ttyACM0
```

`make install` は必要なビルドを行って実機へ書き込みます。ホストPCへのインストールではありません。`make flash` も同じ動作です。`PORT` はデモで使用したポートへ置き換えてください。既存のアプリケーションは書き換わります。モニター終了は `Ctrl+]` です。

```bash
make build IDF_PATH=/path/to/esp-idf
make install PORT=/dev/ttyACM0 BAUD=460800
make menuconfig
make test
```

対象はESP32-S3に固定しています。`sdkconfig.defaults` は16MB Flash、Octal PSRAM、USB Serial/JTAGコンソールを指定します。生成済みの `sdkconfig` がある場合、defaults変更は既存値を上書きしません。`make menuconfig` で確認してください。物理容量と実機の認識結果は起動ログで照合します。

初回ビルドではコンポーネントレジストリへの接続が必要です。直接依存は `main/idf_component.yml`、解決済みの依存関係は `dependencies.lock` に記録しています。`build/`、`managed_components/`、生成される `sdkconfig` はGit管理対象外です。

## 瞬きと表示更新

瞬きと表情の仕様は[身体反応モデル](../../docs/step1-body-behavior.md)を正とします。旧来の独立したランダム待機を廃止し、乾燥由来の欲求から発生率を計算します。時間定義・状態更新順序・回復時点など、今回の実装判断も同文書に記録しています。

振る舞いは10msごとに時刻を確認し、待ち時間を内部に持たず開き具合を返します。表示タスクには長さ1のキューで最新状態のみ渡すため、古いフレームが溜まりません。DMA待機は表示タスクに限定し、将来の入力処理と独立させています。描画・転送が遅れた場合は途中のフレームを省略します。表示は約20ms周期（50fps目安、RTOSのtick単位で丸め）を目標にします。実測値は `eyes_perf` ログの `render_avg`（描画）、`transfer_avg`（転送待ちを含む）、`work_max`（1フレーム処理の最大時間）にマイクロ秒で表示します。アイドル時間を除いた処理時間であり、パネルの実測FPSではありません。20,000µsを超えると目標周期に間に合いません。

通常は丸い白目と瞳を表示します。閉眼中は白目を縦に閉じ、黒い瞳をその輪郭でクリップします。閉じ切ったときは黒背景になります。初回だけ背景全体を消去し、その後は両目と目の間を含む固定矩形を更新します。旧実装の「計算して20行ずつ片目から送る」方式を改め、両目をバッファへ描き終えてから1回の描画要求で転送します。平方根は輪郭の1pxだけに限定し、ほかは二乗距離と行・列ごとの事前計算で判定します。

この変更は描画途中の状態や左右更新の時間差を減らすものです。パネルのTE信号に同期していないため、画面走査とのずれによるティアリングを完全に除去する保証はありません。改善後もちらつく場合は、処理時間ログと実機映像を基に切り分けます。

## 表示初期化の根拠と差分

ユーザーが表示成功を確認したローカル公式サンプル：

`~/esp/ESP32-S3-Touch-AMOLED-2.41-V2/02_Example/ESP-IDF/09_LVGL_V9_Test`

基板定義・初期化コマンドは、Waveshare公式リポジトリのコミット `0eaf16cfae3440d445f2b6caa71e94065094551f` の `components/main_config/main_config.h` と `components/lcd_bsp/display_bsp.c` を根拠にしています。[公式ソース](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2/tree/0eaf16cfae3440d445f2b6caa71e94065094551f/02_Example/ESP-IDF/09_LVGL_V9_Test)。

- ネイティブ座標450×600に、論理座標600×450をソフトウェアで90度回転して送ります。パネルを横向きに置いてください。取り付け方向によっては180度の調整が必要です。
- 公式のQSPI 40MHz、RGB565、コマンド表、EXIO0のリセット順序を維持します。画面タッチ用EXIO1は操作しません。
- 初期化時の輝度を0にして、背景と目を描いた後に約50%へ上げます。
- RGB565は上位バイトから送信します。内部DMAバッファは両目を含む186×476画素の177,072バイトで、転送完了後に再利用します。初期化失敗・転送タイムアウトはログを伴って停止します。
- 起動時のリセット待機は表示タスク内に限定します。
- Wi-Fi、BLE、画面タッチ、外部電極、IMU、RTC、microSDは初期化しません。表示リセットに必要な共有I2Cバスは使用します。

## ホスト側テストとプレビュー

`make test` はESP-IDFや実機なしで、既存の描画検証、瞬きの時系列、身体状態・刺激・確率・表情合成・不応期・回復回数を検証します。同一シードとイベント時刻では、呼び出しの分割が異なっても内部状態と行動時刻が一致することを確認します。シミュレーションは下記条件で自動実行します。

### 9項目の設定とイベント注入

`behavior/body_behavior.h` の `BehaviorParameters` が9項目の既定値を持ちます。意味と初期値の一覧は[仕様書のパラメータ表](../../docs/step1-body-behavior.md#調整可能な振る舞いパラメータ)に集約しています。実機では `app_main.cpp` の `parameters` を変更して再ビルドします。時間の単位はすべて秒です（150msは `0.150f`）。例えば：

```cpp
utsushimi::BehaviorParameters parameters;
parameters.squint_strength = 0.4f;
utsushimi::BodyBehavior body({[](void *) { return esp_random(); }, nullptr}, parameters);
```

時間の項目は有限な0.01〜3600秒、残る5項目は有限な0〜1を受け付けます。これは数値処理上のガードであり、全範囲での自然さや平均間隔を保証しません。`valid()` で検査でき、不正な設定は構築時に全9項目を既定値へ戻します。実行中の設定変更・保存UIは追加していません。

ホスト側では固定シードの乱数コールバックを注入します。テストの `Rng` は非ゼロシードのxorshift32です。実機は `esp_random()` を使用しますが、暗号用途には使いません。乱数コールバックは必須で、一様な32bit値を返す契約です。

```cpp
body.advance(elapsed_us);  // 前の入力から現在まで進める
body.input(utsushimi::BodyEvent::touch_started);
body.input(utsushimi::BodyEvent::petting_detected);
// 継続保持中はadvance()だけで快方向の刺激が続く
body.input(utsushimi::BodyEvent::touch_ended);
eyes.openness = body.openness();
```

イベントは呼び出し時点のモデル時刻で適用します。同じタスクから呼ぶAPIであり、スレッドセーフではありません。今回の実機ループにはタッチ入力源がないため、撫で・保持は自動生成しません。将来の入力層は意味イベントを身体タスクへ渡す必要があります。描画側へセンサー値は渡しません。

### 平均間隔の較正・再現

判定は `p = 1 - exp(-lambda * dt)`、`lambda = gain * (5 / blink_interval) * eye_moisture_need` としました。内部係数 `gain=2.03 /s` を既定9パラメータ・無接触で較正しています。これは10個目のユーザー調整項目ではありません。

較正はシード1001〜1004、各6時間、先頭10分をウォームアップ、内部刻み10msで開始時刻の差を計測しました。係数3.0で4.489801秒、2.0で5.021656秒と挟み込み、2.03で **5.002257秒**（17,267間隔）となりました。ダブル内部の2回目は行動開始と数えず、単発・二連の長さと終了後の不応期を含めます。

較正に使わないシード9001〜9008での検証結果：

- 8試行 × 各12時間（各試行に別途10分のウォームアップ）。両端の開始がウォームアップ後にある69,050間隔を評価。
- 呼び出し周期5 / 10 / 20 / 37msを各2試行。内部刻みは10msで共通。
- 全間隔平均 **5.004402秒**、間隔の標準偏差 **1.628215秒**。
- シード別平均は4.979875〜5.015793秒、シード平均の標準偏差0.011736秒（母標準偏差）。
- 各試行のダブル比率9.84〜10.11%（ウォームアップを含む行動数）。
- 合格条件：全体平均5秒±0.10秒、各試行平均±0.15秒、各試行ダブル比率10%±1.5ポイント。

`make test` は独立シードの検証を実行し、試行数・平均・標準偏差・比率を表示します。実機の `body` ログには累積行動数・ダブル数・最後の開始モデル時刻・回復後の乾燥度・快不快を出します。休止時間を捨てた場合はモデル時刻と壁時計がずれるため、警告のない通常動作区間で間隔を評価してください。較正用の条件は次で再実行できます。

```bash
g++ -O2 -std=c++17 -Wall -Wextra -Werror -Ifirmware/main \
  firmware/main/behavior/blink.cpp firmware/main/behavior/body_behavior.cpp \
  firmware/tests/body_behavior_test.cpp -o /tmp/utsushimi-body-test
/tmp/utsushimi-body-test --calibrate
```

係数を調整するときは `body_behavior.cpp` の内部定数を変更して較正用試行を再実行し、その後、独立シードで評価します。`blink_interval` 以外に `blink_duration` や `blink_recovery` を変えても間隔分布は変化します。5秒の精度保証は既定設定に限り、パラメータ変更時は再較正が必要です。ホストとESP32の浮動小数点実装の差により、同じシードでも異機種間でのビット一致までは保証しません。

プレビューを保存する場合は、リポジトリのルートで以下を実行します。

```bash
g++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/main \
  firmware/main/display/eye_renderer.cpp firmware/tests/eye_renderer_test.cpp \
  -o /tmp/utsushimi-eye-test
/tmp/utsushimi-eye-test /tmp/utsushimi-eyes.ppm
```

## 確認状況

- ユーザー報告：ESP-IDF v5.5.6で `hello_world` と公式V2表示デモが動作。
- 身体反応モデル版：ESP-IDF v5.5.6で `make build` 成功、`make test` 成功。既存の表示初期化・転送・描画コードと依存関係を維持しています。この版の実機書き込み・動作確認は未実施です。初版のちらつき報告に対する実機評価は引き続き必要です。
- 実機では、目の左右配置・向き・白黒表示、瞬きの滑らかさと単発・二連、メモリ容量ログ、USB電源再接続とRESET後の再表示を確認してください。
- 実機で150msの瞬きが認識できるか、二連の100ms間隔、撫で・保持入力注入時の細目と中立復帰、処理時間ログを確認してください。負の快不快専用の形状は追加していません。
- Step 1.2の視線移動・表情の実機調整、Step 1.3以降の実電極の校正・意味イベント生成と接続は残っています。[ロードマップ](../../docs/roadmap.md)を参照してください。
