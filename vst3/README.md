# 刺さりを抑えるソフト（VST3 版 / SasariOsaeru）

女性ボーカルの「さ行」、ハイハットの刺さり、歪みギターの耳障りな高音など、**耳に刺さる音が出た瞬間だけ、その周波数だけ**を下げる VST3 プラグインです。
Android アプリ版と同じ考え方・同じ帯域配置で、Android では作れなかった機能（先読み、Delta モニター、本物の Mix / Range、Mid/Side、実際のリダクション表示）を加えています。

## インストール

| OS | 置き場所 |
|---|---|
| Windows | `SasariOsaeru.vst3` フォルダごと `C:\Program Files\Common Files\VST3\` へ |
| macOS | `SasariOsaeru.vst3` を `/Library/Audio/Plug-Ins/VST3/`（または `~/Library/Audio/Plug-Ins/VST3/`）へ。署名していないため、初回は `xattr -dr com.apple.quarantine <パス>/SasariOsaeru.vst3` が必要な場合があります |
| Linux | `~/.vst3/` へ |

DAW でプラグインを再スキャンすると「SasariOsaeru」（エフェクト／Dynamics・EQ）として表示されます。

ビルド済みファイルは GitHub の Actions → 「VST3 (SasariOsaeru)」→ 最新の実行 → Artifacts から OS ごとにダウンロードできます。

## 画面と操作

- **プリセット**（6 種）／**Normal・Filter・Delta**（モニター切替）
  - Filter：選択中のターゲットの帯域だけを聴く（刺さる帯域を探す）
  - Delta：削った成分だけを聴く（削りすぎていないか確認する）
- **カーブ**：選択中のターゲットの効き方（横軸は「その帯域の普段のレベル比」）
- **帯域表示**：1.6〜16 kHz の細い帯域ごとに、担当ターゲットの色、いまの「普段比」（バー）、しきい値（白線）、**実際のリダクション量（上から下がる赤）**
- **ターゲット**：歪みギター（2.5〜5 kHz、ゆっくり反応）／さ行（5〜10 kHz、速い）／ハイハット（8〜16 kHz、速い）
  - LOW / HIGH：帯域、PROCESSING：反応しやすさ、INTENSITY：下げる強さ（レシオ 1:1〜20:1）
- **共通**：SHARPNESS（効き始めの鋭さと速さ）、MIX（本物のドライ/ウェット）、RANGE（1 帯域あたりの最大リダクション、初期値 12 dB）、OUTPUT
- **高域シェルフ（常時）**：指定周波数より上を常に下げる
- **検出の細かさ**：1/3 オクターブ×10 本／1/6 オクターブ×20 本
- **処理するチャンネル**：ステレオ（左右連動）／Mid のみ（中央＝ボーカル向け）／Side のみ（左右＝シンバル向け）
- **オート**：曲を聴きながら各帯域の「普段のレベル」を約 10 秒かけて学習し、それより突出した瞬間だけを下げます（初期値 ON）

ノブは上下ドラッグ、ダブルクリックで初期値に戻ります。

## 仕組み

- 短時間フーリエ変換（約 10.7 ms の窓、75 % オーバーラップ）で帯域ごとのレベルを測り、帯域ごとに独立したコンプレッサーで下げます。
- 遅延（44.1/48 kHz で 512 サンプル ≒ 10.7 ms、96 kHz で 1024、192 kHz で 2048）を DAW に報告するので、DAW 側で自動補正されます。この遅延のおかげで、歯擦音が始まる瞬間にはすでに下がっています（先読み）。
- バイパス時も同じ遅延を保ちます。
- 何も抑えない設定・Mix 0% では入力と完全に同じ音（遅延分だけずれる）になることをテストで確認しています。

## テスト

- `EngineTests`（JUCE なしの信号処理テスト、38 項目）：44.1/48/96/192 kHz での透過性と遅延、大きな 7 kHz の低減と 500 Hz の不変、Range の上限、初期設定で小さな高域は触らないこと、Mix 0 の透過、Normal＋Delta＝入力、Filter のソロ、Mid/Side、オートの学習（持続音は「普段」として解放、短いバーストは低減）、設定の連続変更、モノラル。
- `PluginHostTest`：ビルドした `.vst3` を DAW と同じ方法で読み込み、チャンネル構成・遅延・パラメータ・音声処理・プリセット・状態の保存/復元を確認（AddressSanitizer 付きでもエラーなし）。
- GitHub Actions では、Windows / macOS / Linux でビルドとテストに加え、プラグイン検証ツール pluginval（strictness 5）を実行します。

## ビルド

CMake 3.22 以上と C++20 コンパイラが必要です。JUCE 8.0.12 は自動で取得されます。

```sh
cmake -S vst3 -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
# → build/SasariOsaeru_artefacts/Release/VST3/SasariOsaeru.vst3
```

## ライセンスについて

JUCE 8 を使っています。JUCE は AGPLv3 または JUCE の商用ライセンス（一定の売上以下は無料の Starter）で利用できます。個人で使う分には問題ありませんが、配布・販売する場合は JUCE のライセンス条件を確認してください。
