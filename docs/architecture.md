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
        | 表情状態
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

まず安定した接触検出を優先します。Step 1には方向・時間的パターンの検出と `petting_detected` の生成を含めます。具体的な時間範囲、接触の重なりの扱い、イベントに含めるデータ、連続イベントの扱いは実装時に決め、実際の入力で調整します。手を置いた状態を、そのまま撫で動作とみなしてはいけません。

`Behavior` は、接触による身体状態・表情の変化と、自動生成される瞬きの時間進行を管理します。これは即時的な身体反応であり、永続的な人格や学習ではありません。ハードウェアの割り当ては[ハードウェア](hardware.md)、対象範囲と完了条件は[ロードマップ](roadmap.md)に集約します。

## Step 1の身体状態・表情・自動運動

Step 1は身体状態`valence`と接触刺激を扱い、快状態から目を細めるExpressionを生成する。撫でイベントは`valence`を上昇させ、表情に間接的に反映される。瞬きは`Automatic Movement`として欲求・明示的な意思決定を必要とせず生成する。目の乾燥度と乾燥由来の欲求・不快はモデル化しない。

概念上は身体状態・欲求・判断・行動の責務分離を維持するが、Step 1には欲求駆動の行動がないため、空のDesire/Decision層を実装しない。将来の欲求駆動ActionとAutomatic Movementは、同じ身体能力を利用し得るが、調停仕様は後続ステップで決める。Automatic Movementは将来の微小眼球運動や呼吸様運動へ拡張可能な概念とし、Step 1では瞬きのみ実装する。

表情と瞬きは独立して進行し、EyeRendererで合成する。瞬きは不応期後の経過時間に応じて発生率が増加するワイブル分布（形状 k=2）を用い、身体状態には依存させない。`BodyBehavior` が入力・身体状態・時間進行を管理し、`Automatic Movement` モジュールが瞬きの発生判定・不応期・アニメーションを担当する。汎用運動登録機構はStep 1では設けない。詳細は[Step 1 身体反応モデル](step1-body-behavior.md)を参照する。

## 目の設計と内部モジュール

2026-10-18の方針変更により、丸い白目と丸い黒い瞳による、少しかわいらしい記号的な目を採用します。従来の哺乳類・猛禽類のような虹彩やハイライトを持つ写実的な目の方針を置き換えます。黒背景に左右2つの目を配置します。

現在の表情状態は `gaze_x`、`gaze_y`、`openness`、`pupil_size` に加え、快のまぶた形状を表す `upper_lid`・`lower_lid` を扱います。後者は描画用の幾何パラメータで、快不快や時刻を描画層へ渡しません。採用した形状と表情追従は[身体反応モデル](step1-body-behavior.md#採用した快のまぶた形状)を参照してください。白目と瞳は数値から描き、瞬きは白目の縦方向の開き具合で表現します。画像切り替えには依存しません。視線は現段階では固定です。Automatic Movementとしての単発・二連の瞬きの実装と表示更新方式は[ファームウェア](../firmware/main/README.md#瞬きと表示更新)に集約します。

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

上記の表は2026-10-17の公式サンプル調査記録です。現在の独自ファームウェアの依存バージョンは `firmware/main/idf_component.yml` と `firmware/dependencies.lock` を正とします。ユーザーはv5.5.6で公式V2デモの表示成功を確認済みです。残りの確認事項は、独自ファームウェアの表示の向き・色・リセットとメモリ認識の確認、実機でのC++統合の評価、アニメーション応答性の測定です。これらを踏まえて開発構成を確定します。外部電極用の内蔵タッチドライバー・APIも、採用するIDFに合わせて選定・検証する必要があります。

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
