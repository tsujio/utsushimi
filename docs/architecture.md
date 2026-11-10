# アーキテクチャ

## 基本概念

UTSUSHIMIはKOKOROに身体を与えます。

```text
KOKORO
  |
  | 知性／人格／長期記憶
  v
UTSUSHIMI
  |
  | 感覚／表情／声／身体状態
  +-- 任意の回転台座
  +-- 任意の移動台座
```

KOKOROは特定の身体に縛られず、将来的にはPC、スマートフォン、別のロボットへ移動できます。UTSUSHIMIの身体を交換・喪失しても、KOKOROが持つ同一性、人格、主要な記憶が失われない設計とします。

## 責務の境界

### KOKORO

KOKOROは以下を担当します。

- 長期記憶
- エピソード記憶・意味記憶
- 人格・好み
- 関係性
- 経験からの学習
- 高度な意思決定
- 自然言語処理
- 記憶の定着・忘却

### UTSUSHIMI

UTSUSHIMIは以下を担当します。

- 触覚
- 聴覚
- 視覚
- 身体状態
- 視線
- 表情
- 発声
- 回転・移動能力
- 即時的・反射的な反応
- 一時的な作業状態・身体状態

ネットワークに接続していなくても、基本的な身体的コミュニケーションを実行できる構成とします。

## 身体イベント

通常、生のセンサーデータをKOKOROや上位の振る舞いへ直接公開しません。

Step 1の処理の流れは以下のとおりです。

```text
静電容量の生の測定値
        |
        v
TouchSensor / PettingDetector
        |
        | touch_started / touch_ended / petting_detected
        v
Behavior
        |
        | 表情・視線状態
        v
EyeRenderer
        |
        v
AMOLED
```

生の測定値は入力・検出層で扱い、必要に応じて診断用に参照します。上位の振る舞いは意味を持つ身体イベントを受け取ります。センサーコードから描画コードを直接呼び出してはいけません。将来KOKOROと連携する際も、特定のセンサー実装に依存せず、イベントを経験として扱えるようにします。

解釈の方向性は以下のとおりです。

| 物理的な観測 | 意味 |
| --- | --- |
| 単一電極への接触／離脱 | `touch_started` / `touch_ended` |
| A → B → C | 一方向への撫で動作 |
| C → B → A | 逆方向への撫で動作 |
| 複数電極への継続接触 | 手を置いている状態。詳細は後で調整 |

まず安定した接触検出を優先します。Step 1には方向・時間的パターンの検出と `petting_detected` の生成を含めます。現在の暫定的な時間範囲・重なり・イベントの受け渡しは[タッチ入力と撫で判定](step1-body-behavior.md#タッチ入力と撫で判定暫定)を参照してください。手を置いた状態を、そのまま撫で動作とみなしてはいけません。

`Behavior` は、接触による身体状態・表情の変化、接触開始時の視線反射と、自動生成される瞬きの時間進行を管理します。これは即時的な身体反応であり、永続的な人格や学習ではありません。ハードウェアの割り当ては[ハードウェア](hardware.md)、対象範囲と完了条件は[ロードマップ](roadmap.md)に集約します。

## Step 1の身体状態・表情・自動運動

Step 1は身体状態`valence`と接触刺激を扱い、Expressionがヒステリシス付きの通常・心地よい状態を選び、目の開き具合を滑らかに遷移させる。撫でイベントは`valence`を上昇させ、表情に間接的に反映される。瞬きは`Automatic Movement`として欲求・明示的な意思決定を必要とせず生成する。目の乾燥度と乾燥由来の欲求・不快はモデル化しない。

概念上は身体状態・欲求・判断・行動の責務分離を維持するが、Step 1には欲求駆動の行動がないため、空のDesire/Decision層を実装しない。将来の欲求駆動ActionとAutomatic Movementは、同じ身体能力を利用し得るが、調停仕様は後続ステップで決める。Automatic Movementは将来の微小眼球運動や呼吸様運動へ拡張可能な概念とし、Step 1では瞬きのみ実装する。

表情と瞬きは独立して進行し、EyeRendererで合成する。瞬きは不応期後の経過時間に応じて発生率が増加するワイブル分布（形状 k=2）を用い、身体状態には依存させない。`BodyBehavior` が入力・身体状態・時間進行を管理し、`Automatic Movement` モジュールが瞬きの発生判定・不応期・アニメーションを担当する。汎用運動登録機構はStep 1では設けない。詳細は[Step 1 身体反応モデル](step1-body-behavior.md)を参照する。

## 目の設計と内部モジュール

丸い白目と丸い黒い瞳による、少しかわいらしい記号的な目を採用します。黒背景に左右2つの目を配置します。

現在の表情状態は `gaze_x`、`gaze_y`、`openness`、`pupil_size` に加え、快のまぶた形状を表す `upper_lid`・`lower_lid` を扱います。後者は描画用の幾何パラメータで、快不快や時刻を描画層へ渡しません。採用した形状と表情追従は[身体反応モデル](step1-body-behavior.md#採用した快のまぶた形状)を参照してください。白目と瞳は数値から描き、瞬きは白目の縦方向の開き具合で表現します。画像切り替えには依存しません。視線は接触開始時にBehaviorが一過性の上方向への反射を管理し、`EyeState.gaze_y` を通じて瞳だけを移動させます。保持判定は入力層、時間進行はBehavior、クリッピングを含む描画はEyeRendererが担当します。視線は身体状態・Expression・瞬きから独立して進行し、詳細は[接触反射の仕様](step1-body-behavior.md#接触反射上方向への視線移動)に集約します。Automatic Movementとしての単発・二連の瞬きの実装と表示更新方式は[ファームウェア](../firmware/main/README.md#瞬きと表示更新)に集約します。

瞬きと視線移動は、長い待機で処理を止めず、経過時間に応じて進めます。アニメーション中もタッチ測定とイベント処理を継続します。ハードウェア固有の入力取得と画面転送は、小さなインターフェースの背後に置きます。

推奨するモジュール境界は以下のとおりです。ディレクトリ構成を強制するものではありません。

```text
firmware/main/
  app_main.*
  display/eye_renderer.*
  touch/touch_sensor.*
  touch/petting_detector.*
  behavior/behavior.*
  events/body_event.*
```

## ソフトウェア候補と互換性

**現在の採用状況（2026-10-18）：** Step 1.1はESP-IDF v5.5.6 + C++（基板初期化はC）で実装しました。目はRGB565を直接生成するため、LVGLは導入していません。描画側はハードウェアに依存せず、LVGLの将来採否は応答性と実装の簡潔さを評価して決めます。ビルド・検証状況は[ファームウェア](../firmware/main/README.md)を参照してください。CのBSP・ドライバーを身体機能のインターフェースの背後に置くことはできますが、C++との統合や描画方式は評価が必要です。デモに含まれるという理由だけで、無関係な機能や依存ライブラリを取り込まないでください。

**ソース確認（2026-10-17）：** 公式V2リポジトリのコミット `0eaf16cfae3440d445f2b6caa71e94065094551f` にある `02_Example/ESP-IDF/09_LVGL_V9_Test` を調査しました。環境のインストール、ビルド、実機試験は行っていません。

| 項目 | 公式サンプルで確認した内容 |
| --- | --- |
| ESP-IDF | `sdkconfig` と `dependencies.lock` はともに5.5.4。対象は `esp32s3` |
| LVGL | マニフェストは `^9.5.0`、ロックファイルは9.5.0 |
| LVGL連携 | `espressif/esp_lvgl_adapter` の固定版は0.4.3~1。IDF >=5.5が必要 |
| 表示ドライバー | `espressif/esp_lcd_sh8601` の固定版は2.0.1~1。IDF >=5.3が必要 |
| I/Oエキスパンダードライバー | `espressif/esp_io_expander_tca9554` の固定版は2.0.3 |
| 画面タッチドライバー | `espressif/esp_lcd_touch_ft5x06` の固定版は1.1.0~1。外部電極の内蔵タッチ機能とは別 |
| 表示初期化 | QSPIと基板固有のコマンド表を使用。リセットはI2Cエキスパンダー経由 |
| メモリ設定 | Octal PSRAMが有効。サンプルのFlash設定は2 MBのみ |

マニフェストにあるIDFの直接の下限 `>=4.1.0` だけでは、解決済みの依存関係全体を表せません。間接的な依存ライブラリが、より新しいIDFを要求するためです。調査したバージョンの組み合わせから、IDF 5.5.4 / LVGL 9.5.0は **動作再現用の候補** として整合していますが、ビルド・動作成功の証明ではありません。

[WaveshareのESP-IDFガイド][idf-guide]はV2にIDF 5.5.xを推奨し、設定が5.5.2、ロックファイルが5.5.4と説明しています。一方、調査したコミットでは両方とも5.5.4でした。古いV1向け最低バージョンや未固定の最新版ではなく、固定したソースリビジョンを再現の基準にします。

[ハードウェア](hardware.md)に記載したコントローラー名とソフトウェアドライバー名は、公式サンプルでも意図的に異なります。`display_bsp.c` は独自の初期化コマンドとともに `esp_lcd_new_panel_sh8601` を呼び出します。最初のデモ確認ではこのV2初期化経路を維持し、汎用ドライバーの既定値だけで動作すると仮定しないでください。また、サンプルのFlash設定2 MBを基板の物理容量と混同せず、UTSUSHIMIへの採用前にFlash容量設定、パーティション構成、PSRAM設定を確認します。

上記の表は2026-10-17の公式サンプル調査記録です。現在の独自ファームウェアの依存バージョンは `firmware/main/idf_component.yml` と `firmware/dependencies.lock` を正とします。ユーザーはv5.5.6で公式V2デモの表示成功を確認済みです。残りの確認事項は、独自ファームウェアの表示の向き・色・リセットとメモリ認識の確認、実機でのC++統合の評価、アニメーション応答性の測定です。これらを踏まえて開発構成を確定します。外部電極の初期診断はESP-IDF v5.5.6の `esp_driver_touch_sens` を使用します。APIの根拠と実装・実機検証の範囲は[ファームウェア](../firmware/main/README.md#電極aの初期動作確認)を参照してください。

参照資料：

- [Waveshare V2向けESP-IDFガイド][idf-guide]
- [調査した固定リビジョンの公式V2サンプル][sample]
- [表示コンポーネントのマニフェスト][manifest]
- [解決済みの依存関係][lock]
- [プロジェクト設定][sdkconfig]
- [表示初期化のソース][display]

[idf-guide]: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.41/ESP-IDF
[sample]: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2/tree/0eaf16cfae3440d445f2b6caa71e94065094551f/02_Example/ESP-IDF/09_LVGL_V9_Test
[manifest]: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2/blob/0eaf16cfae3440d445f2b6caa71e94065094551f/02_Example/ESP-IDF/09_LVGL_V9_Test/components/lcd_bsp/idf_component.yml
[lock]: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2/blob/0eaf16cfae3440d445f2b6caa71e94065094551f/02_Example/ESP-IDF/09_LVGL_V9_Test/dependencies.lock
[sdkconfig]: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2/blob/0eaf16cfae3440d445f2b6caa71e94065094551f/02_Example/ESP-IDF/09_LVGL_V9_Test/sdkconfig
[display]: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2/blob/0eaf16cfae3440d445f2b6caa71e94065094551f/02_Example/ESP-IDF/09_LVGL_V9_Test/components/lcd_bsp/display_bsp.c

## 身体APIの方向性

将来の能力は、以下のような意味を持つ操作として公開する想定です。

- `look(direction)`
- `expression(type)`
- `chirp(type)`
- `speak(text)`
- `rotate(angle)`
- `move(direction, speed)`

ローカルな反射、状態機械、KOKOROが、将来的に同じ身体能力の抽象化を利用できるようにします。

外部デバイスは独立したコントローラーを持ち、特定の台座モデル専用のプロトコルではなく、能力を通じて交換・追加できる構成を目指します。ハードウェアと通信方式の候補は[ハードウェア](hardware.md#外部身体デバイス)に記載します。メッセージ形式、能力の検出、エラー処理、通信の詳細は未決定で、Step 1の対象外です。

## Step 2.1の音声出力

`audio/audio_output` は身体の出力能力として短音要求を受け取り、コピーしたパラメータを有界キューから専用タスクへ渡します。呼び出し元はI2S DMAの待機を行いません。波形生成は `audio/tone.h`、ハードウェア固有のI2S送信は `audio_output.cpp` が担当します。将来の鳴き声・非言語音の呼び出し元からも `play_tone` を利用できます。音源ファイル、自然言語処理や判断層は追加しません。通常の身体イベントにはまだ発声を接続せず、明示的な診断設定だけで試験します。[ハードウェア条件](hardware.md#step-21max98357aの音声出力)と[ファームウェアの手順](../firmware/main/README.md#step-21音声テスト)を参照してください。

## Step 2.2の音声入力と共有I2S

`audio_output.cpp` がI2S0を所有し、入力有効時は `i2s_new_channel` の1回の呼出しでTX/RXを同時確保します。両ハンドルを同一の48kHz・32bit・ステレオ・Philips設定で順に初期化し、TX→RXの順に有効化します。MCLKは外部へ出力しません。[ESP-IDF v5.5.6の全二重API](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/peripherals/i2s.html#full-duplex)とローカルの同版ドライバーで共有BCLK/WSを確認しています。APIはチャネルごとの初期化・有効化が必要で、read/write呼出し自体の同期は保証しません。今回はエコー処理や送受信サンプルの時刻合わせを必要としません。

電子音の終了時にもTX/RXを止めず、TX専用タスクが無音PCMを送ります。DMA不足時は既存の自動ゼロクリアを使用します。入力を明示的に無効にした音声試験ではTXのみを使用し、電子音終了時に停止します。

RX専用タスクはステレオDMAを読み、左の有効24bitだけを `input_rms.h` へ渡します。4800サンプルごとに平均を除去したRMSをログへ出します。RMSは入力診断であり、意味イベントやBehavior・描画には接続しません。送受信とも優先度0で、身体・描画・タッチのタスクを待たせません。入力有効時は各方向に5ms×8枚のDMA領域（40ms分）を確保します。読取り期限は1秒です。取得エラー・受信あふれ時は集計窓と当該ブロックを破棄します。APIエラー・空読取り・不正なバイト数の場合だけ20ms待ち、あふれのみなら待機せずキューを読み進めます。エラーログは最大毎秒1回です。ハードウェア故障の自動再初期化は行いません。固定の作業バッファとDMAを使用し、継続取得中の動的メモリ確保や録音保存は行いません。

## Step 2.3のウェイクワード認識と探索視線

`audio_output.cpp` の単一RXタスクがRMSと認識の音声を共有します。GPIO・共有I2S0の48kHz・32bitステレオは変更しません。認識用に左24bitを符号付き16bitへスケールし、5ms以下のブロックを長さ32の有界キューへ待機なしでコピーします。RMSは48kHzの元データから引き続き計算します。専用の優先度0・スタック8192バイトの認識タスクだけが変換器とWakeNetを所有します。

認識タスクでは127タップBlackman窓sinc FIR（遮断中心6.5kHz、DCゲイン1）を通して3分の1に間引きます。群遅延は1.3125msです。係数の対称性を使って1出力64乗算とし、二重化した固定履歴でタップごとの循環境界分岐を避けます。認識入力の翻訳単位は `-O2` とし、fast-mathは使用しません。入力欠落・キュー欠落・250msを超える滞留・時刻異常では、そのブロックと部分フレームを破棄し、FIRとWakeNetの `clean` で履歴を初期化します。FIRのウォームアップ完了までは出力しません。連番は送信失敗でも進め、DMA受信エラーではepochを更新するため、欠落をまたぐ音声フレームを認識しません。I2Sドライバーは追加確保しません。

`audio/wake_word` は描画・探索を知らず、認識結果を長さ1の最新イベントへ渡します。イベントは末尾音声ブロックのRX観測時刻 `audio_end_us` と認識完了時刻 `detected_us` を持ち、どちらも起動からの単調時計です。前者はDMAから受領した観測時刻で、発声開始・厳密な収録時刻ではありません。現構成にはDMA待機・FIR・モデルの遅延が含まれます。将来の方向推定には共通の音声サンプル時刻を整備して関連付ける必要があります。現時点では未使用の方向イベント基盤を作りません。

本体ループは500msより古い認識を捨て、`BodyEvent::wake_word_detected` をBehaviorへ渡します。Behaviorは左右ランダムの方向へ300ms、反対へ400ms、正面へ300msで移動し、各区間をsmoothstepで補間します。振幅は `EyeState.gaze_x` の±0.8（最大±1に制限）、既存の可動域・クリッピングを使い両目を同じ方向へ動かします。時間と振幅は実機調整前の暫定値です。経過時間で非ブロッキングに進み、描画は数値だけを受け取ります。

認識タスクはクールダウン内の再検出を通知せず、Behaviorも実行中・クールダウン中は再認識を予約しません。既定の開始間隔は3秒です。タッチ保持中と上目反射中は探索を開始しません。探索中に接触開始したら水平視線はその位置から300msで正面へ戻し、既存の上目反射を優先します。身体状態・快表情・瞬きはリセットしません。今後の高信頼度方向への注視か探索かの選択はBehaviorで行い、認識層へ組み込みません。探索負荷の比較診断では、既定無効の `UTSUSHIMI_WAKE_SEARCH_DISABLED` により身体ループが認識イベントを消費して探索への受け渡しだけを抑制します。認識層・描画層には診断分岐を追加しません。[比較手順](../firmware/main/README.md#探索だけを停止する比較診断)を参照してください。

### ESP-SRの採用根拠と制約

ESP-SR **2.6.1** とESP-DL **3.3.14** をマニフェストで固定し、残る間接依存は `firmware/dependencies.lock` を正とします。取得した公式コンポーネントのソース基準はコミット `b9dc43a329a68de2f72147808fc9ea29d2ace71e`（version 2.6.1）です。[Kconfig](https://github.com/espressif/esp-sr/blob/b9dc43a329a68de2f72147808fc9ea29d2ace71e/Kconfig.projbuild) にESP32-S3用 `SR_WN_WN10_JA_AIRISU` があり、モデルの実ファイルを確認しています。公式マニフェストのIDF制約は `>=5.0` ですが、v5.5.6と現在の固定依存で認識・RMS・電子音併用構成のコンパイル・リンクとモデル生成を確認済みです。これは実機動作の確認ではありません。

[WakeNet API](https://github.com/espressif/esp-sr/blob/b9dc43a329a68de2f72147808fc9ea29d2ace71e/include/esp32s3/esp_wn_iface.h) の `create(name, DET_MODE_90)` はWakeNet10のFlash読み込み・通常モードです。`get_samp_rate`・`get_channel_num`・`get_samp_chunksize` を起動時に取得し、16kHz・1チャンネルと有効なフレーム長を検査してから符号付き16bit PCMを `detect` へ渡します。`WAKENET_CHANNEL_VERIFIED`（-1）は正常な状態通知として処理を継続し、イベント再発行やモデル・PCMのリセットは行いません。正の返値を検出、0を非検出として扱い、定義外の負値だけをエラー扱いにします。`esp_srmodel_init("model")` でモデルパーティションをマップします。公式ローダーがパックヘッダーを信頼するため、ロード前に単一の指定モデル・ファイル名・領域境界を検査します（ペイロードの完全性検証ではありません）。モデル未配置・別モデル・契約不一致はエラーログを出し、認識を行わず入力・身体反応を継続します。

[AFE API](https://github.com/espressif/esp-sr/blob/b9dc43a329a68de2f72147808fc9ea29d2ace71e/include/esp32s3/esp_afe_sr_iface.h) は `afe_config_init("M", ..., AFE_TYPE_SR, ...)`、`create_from_config`、フレーム長を照会した `feed` と `fetch_with_delay` に対応します。今回は1マイクの直接WakeNet APIを採用し、AFE・AEC・AGC・VAD・ノイズ抑制は起動しません。スピーカー自己音の抑制と騒音下の精度は保証せず、実機試験対象です。

モデルの生ファイル合計は823,110バイト（Flash用p1とPSRAM用p2の合計）です。今回のFlash用p1パックは391,867バイトで、公式パッカーの推奨領域は384KiBです。Flashにはfactory 6MiB、model 2MiBを確保し、OTAは実装しません。[公式ベンチマーク](https://github.com/espressif/esp-sr/blob/b9dc43a329a68de2f72147808fc9ea29d2ace71e/docs/en/benchmark/README.rst) のS3 WakeNet10・3chは内部RAM 17KB、PSRAM 523KB、32msフレームあたり7.1msですが、これは今回の「アイリス」1chの必要量を保証しません。8MB PSRAMを有効のまま、ロード前後の内部RAM・PSRAM差分、10秒ごとの空き・内部最小空き・認識スタック最小余裕を記録します。追加のPCMキューは約16KiB、タスクスタック8KiB、認識フレームは最大8KiB、FIRは固定領域です。実機のモデル初期化では内部RAM約14KB・PSRAM 384,800バイトの空き減少を観測し、既存121,104バイトの内部DMA描画領域との短時間の共存を確認しています。初期化には他タスクも動くため、差分は厳密な単独必要量ではありません。実際の最大メモリと長時間の余裕は引き続き評価対象です。

ESP-SR 2.6.1の依存制約 `esp-dl: ~3.3.12` だけでは、WakeNet配布バイナリのC++ ABIを保証できません。S3のWakeNetXは `dl::Model` に144バイトを確保します。ESP-DL 3.3.15には末尾の `m_built` が追加され、コンストラクターがオフセット144へ書き込むため、この組み合わせはヒープを破損します。3.3.14を直接依存として厳密固定し、認識コードで `sizeof(dl::Model) == 144` を静的検査します。根拠は取得した公式コンポーネントのクラス定義と、S3用 `libwakenet.a` およびコンストラクターの逆アセンブルです。3.3.14は当該クラスサイズとWakeNetが必要とする音声APIを維持します（公式コンポーネントのコミット `5acd1c9992af4afb0fa80c221a676982b11381b8`）。今後の更新時はリンク成功だけで互換と扱わず、配布バイナリとのABIと実機初期化を確認します。

### 探索時の負荷の調査

描画は優先度4、認識・I2S送受信は優先度0、身体更新は優先度1です。描画は `xTaskCreatePinnedToCore` でCPU0、認識はCPU1へ固定します。ESP-IDF v5.5.6のFreeRTOSはFPU初使用時に未固定タスクを実行中のCPUへ自動固定するため、起動時の実行順序へ依存させずCPUを分離します。自動割り当て条件の提供ログ `step23-task-load-20261109-222748.log` では描画と認識のaffinityがともに1で、探索中に描画が同コアの約38%を使う区間と、認識の実行時間低下・キュー欠落を確認しています。この計測を採用理由とします。CPU分離後の提供ログでは10回の認識・探索が完了し、約3分55秒に音声欠落はありません。確認条件と未確認範囲は[実機検証状況](../firmware/main/README.md#step-23の実機検証状況)に集約します。30Hzの描画目標、各優先度、認識キュー容量、I2S設定は維持します。CPU0側の身体更新・タッチと描画の共存も再確認します。描画の実行CPU・集計区間を `eyes_perf` と起動時に記録します。CPU使用・実行待ち・共有資源の影響は区別して調査します。

`UTSUSHIMI_TASK_LOAD_DIAGNOSTICS` は既定無効の診断です。ESP timerをクロックとするFreeRTOS実行時間統計を有効にし、身体ループから1秒間隔で長寿命タスクの情報を取得します。スタック走査・全タスク一括スナップショット・新規タスク・動的確保は行いません。探索を含んだ区間と約10秒ごとの平常区間のみ、CPU affinity・優先度・区間内のスケジュール実行時間を出します。割り込み実行時間等を含む概算であり、厳密な演算時間ではありません。計測とログ自身にも負荷があるため、比較試験では設定を揃えます。[手順と判定](../firmware/main/README.md#タスク実行時間とcpu割り当ての診断)を参照してください。診断機能自体は優先度・CPU割り当て・描画頻度・バッファ容量を変更しません。

根拠はローカルESP-IDF v5.5.6の `docs/en/api-reference/system/freertos_idf.rst`（Floating Point Usage）、`FreeRTOS-Kernel/tasks.c` の `vTaskGetInfo`、`task.h` の `TaskStatus_t` と、[同版の公式FreeRTOS文書](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/system/freertos_idf.html#floating-point-usage)です。
