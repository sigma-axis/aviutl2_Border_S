# Border_S AviUtl ExEdit2 フィルタプラグイン

機能拡張した縁取り効果や，角丸めなどの機能を提供する AviUtl ExEdit2 フィルタプラグインです．[AviUtl 無印版の縁取りσ](https://github.com/sigma-axis/aviutl_CircleBorder_S)の AviUtl2 移植版です．

標準の縁取り効果に比べて，大きな半径で縁取りしても境界がぼやけず，高速に動作します．縁取りの縦横比や透明度の指定，輪部の曲率を調整したり，スーパー楕円での縁取りなども可能です．縁取り幅も小数点以下2桁まで細かく指定できます．

また，「角丸めσ」で任意の形状の図形の角を丸めたり縁部分を削ることもできます．[「四隅丸め」](https://github.com/sigma-axis/aviutl2_script_Basic_S/wiki/四隅丸め)とは違い，オブジェクトの不透明ピクセルの境界に沿って丸めるため，複雑な形状のオブジェクトにも対応できます．

「アウトラインσ」では縁に沿った幅のあるラインを生成できます．ギャップの空いた縁取りや，元画像の内側に沿ってラインを引いたりと，多様な装飾ができます．

[ダウンロードはこちら．](https://github.com/sigma-axis/aviutl2_Border_S/releases) [紹介動画](https://www.nicovideo.jp/watch/sm46116909)

<img width="1080" height="880" alt="Border_S で追加されるフィルタのデモ一覧" src="https://github.com/user-attachments/assets/c8229415-35be-4454-ae10-4aef9c6bfdc7" />

##  お願い

このプラグインを使った動画では，ニコニコの親作品にこのプラグインの紹介動画を登録してくれると嬉しいです．任意ではありますが，登録してくれたほうが励みになります．

- 登録 ID: `sm46116909`


##  動作要件

- AviUtl ExEdit2

  http://spring-fragrance.mints.ne.jp/aviutl

  - `beta39` で動作確認済み．

- Visual C++ 再頒布可能パッケージ (v14 の x64 版が必要)

  https://learn.microsoft.com/ja-jp/cpp/windows/latest-supported-vc-redist

##  導入方法

ダウンロードした `aviutl2_Border_S-v*.**.au2pkg.zip` を AviUtl2 のウィンドウにドラッグ & ドロップしてください．

「フィルタ効果の追加」メニューの「Border_S」以下に「縁取りσ」「角丸めσ」「アウトラインσ」が追加されます．

### For non-Japanese speaking users

You may be able to find language translation file for this script / plugin from [this repository](https://github.com/sigma-axis/aviutl2_translations_sigma-axis).

Translation files enable names, parameters and commands of the scripts / plugins to be displayed in other languages.

Although, usage documentations for this script / plugin in languages other than Japanese are not available now.

##  縁取りσ

オブジェクトの不透明ピクセルの境界に縁取りを追加します．標準の「縁取り」に比べて柔軟な設定ができ，高速です．

<img width="500" height="240" alt="縁取りσ での外側/内側縁取りの例" src="https://github.com/user-attachments/assets/e9466201-d6ea-4f51-ba96-855f7604ed3e" />

### サイズ

縁取りの幅をピクセル単位で指定します．小数点以下2桁まで指定できます．

最小値は 0, 最大値は 500, 初期値は 5.

### ぼかし

縁取りの境界にぼかしをかけられます．ぼかしの大きさを[「サイズ」](#サイズ)からの割合で % 単位で指定します．

最小値は 0, 最大値は 200, 初期値は 0.

### 縁色

縁取りの色を指定します．

初期値は `ffffff` (白色).

### 方式

縁取りの計算に使うアルゴリズムを選択します．

次の選択肢があります．選択肢に応じて[「α調整」](#α調整)の意味が変わります:

| 方式 | サンプル | α調整 | 説明 |
|:---:|:---:|:---:|:---|
| `2値化` | <img width="300" height="240" alt="2値化での結果の例" src="https://github.com/user-attachments/assets/56460022-1a52-4c3e-8509-ca22ed1ebbb9" /> | アルファ値のしきい値を指定 | アルファ値で2値化して計算します．<ul><li>最も高速なアルゴリズム．</li><li>縁取りの境界がジャギーになる．</li></ul> |
| `2値化倍精度` | <img width="300" height="240" alt="2値化倍精度での結果の例" src="https://github.com/user-attachments/assets/5ec1ac87-3cf5-43aa-86ce-6a64b3d789cf" /> | アルファ値のしきい値を指定 | `2値化` に簡易的なアンチエイリアスを適用したものです．<ul><li>`2値化` とほとんど差のない速度．</li></ul> |
| `2値化スムーズ` | <img width="300" height="240" alt="2値化スムーズでの結果の例" src="https://github.com/user-attachments/assets/d43ba1b4-44bf-4a0d-a505-93e3bbad438b" /> | アルファ値のしきい値を指定 | アルファ値で2値化するが，滑らかな境界を設定します．<ul><li>`2値化倍精度` と比べて動作が遅く，特に「サイズ」が大きい場合に顕著．</li></ul> |
| `総和` | <img width="300" height="240" alt="総和での結果の例" src="https://github.com/user-attachments/assets/e050c3cf-5cfa-4958-bced-4c9a1b2be5d5" /> | 縁取り境界のアンチエイリアス幅を調整 | 周辺ピクセルのアルファ値の総和から算出します．<ul><li>しきい値で2値化しないため滑らかな境界に．</li><li>尖った部分の縁取りが小さくなる傾向がある．</li><li>`2値化スムーズ` よりやや遅い．</li></ul> |
| `最大値` | <img width="300" height="240" alt="最大値での結果の例" src="https://github.com/user-attachments/assets/23fd51ad-109f-4b84-981a-df4d10f54eea" /> | 無視されます | 周辺ピクセルのアルファ値の最大値を探します．<ul><li>元画像の境界部分のアルファ値が，そのまま縁取りの境界に現れる．<ul><li>元画像の境界が滑らかなら滑らかに．</li><li>元画像の境界がジャギーならジャギーに．</li></ul></li><li>`総和` よりやや遅い．</li></ul> |

初期値は `2値化スムーズ`.

- どの「方式」を選んでも，標準の「縁取り」よりも高速で動作することを確認しています．

### α調整

[「方式」](#方式)の動作に関する調整を行います．

「方式」に応じて意味が変わりますが，傾向として値が大きいと縁取りがしぼみ，小さいと広がります．

最小値は 0, 最大値は 100, 初期値は 50.

### タイプ

通常の縁取りか，内側縁取りかを選択します．

| タイプ | 説明 |
|:---:| :--- |
| `外側縁取り` | 通常の縁取り． |
| `内側縁取り` | 内側縁取り． |

初期値は `外側縁取り`.

### 透明度

縁取りの透明度を % 単位で指定します．

最小値は 0, 最大値は 100, 初期値は 0.

### 前景透明度

縁取りの元画像の透明度を指定します．透明にすると縁取りに「穴」が空いたような見た目になります．

最小値は 0, 最大値は 100, 初期値は 0.

### 縦横比

縁取りに縦横比を持たせて，楕円形の縁取りを作ることができます．

<img width="320" height="160" alt="縦横比を変えた縁取りの例" src="https://github.com/user-attachments/assets/a2ad45ad-1253-48ee-8cc7-f629aea9264c" />

% 単位で指定，0 で真円，正の値で縦長の楕円形，負の値で横長の楕円形になります．

最小値は -100, 最大値は 100, 初期値は 0.

### 凸半径

縁取り凸部分の最低保証の曲率半径です．この値が大きいほど，凸部分が緩やかになります．

最小値は 0, 最大値は 500, 初期値は 0.

### 凹半径

縁取り凹部分の最低保証の曲率半径です．この値が大きいほど，凹部分が緩やかになります．

最小値は 0, 最大値は 500, 初期値は 0.

### 膨らみ

縁取りの輪郭を[スーパー楕円](https://ja.wikipedia.org/wiki/%E3%82%B9%E3%83%BC%E3%83%91%E3%83%BC%E6%A5%A4%E5%86%86)にするためのパラメータです．この値が大きいほど，輪郭が四角形に近づきます．

<img width="320" height="160" alt="スーパー楕円を利用した縁取りの例" src="https://github.com/user-attachments/assets/4e2c7702-9e4b-423e-b00f-f31ea7b8d899" />

ここでの指定値が % 単位で $p'$ だとすると，スーパー楕円の指数 $p$ は以下のように計算されます:

$$ p = \frac{3 + p'}{3 - p'}. $$

主要な図形とは次のように対応しています:

| 膨らみ | 図形 |
|:---:| :---: |
| `-300` | 十字 |
| `-60` | [アステロイド](https://ja.wikipedia.org/wiki/%E3%82%A2%E3%82%B9%E3%83%86%E3%83%AD%E3%82%A4%E3%83%89) |
| `0` | [菱形](https://ja.wikipedia.org/wiki/%E8%8F%B1%E5%BD%A2) |
| `100` | [楕円](https://ja.wikipedia.org/wiki/%E6%A5%96%E5%86%86) |
| `300` | 長方形 |

最小値は -300, 最大値は 300, 初期値は 100.

##  角丸めσ

オブジェクトの不透明ピクセルの境界に沿って，角を丸めたり縁部分を削ることができます．

<img width="440" height="340" alt="文字への角丸めや画像のふちを縮小した例" src="https://github.com/user-attachments/assets/4cbfd78c-cd93-4278-a22f-d77adde4fcae" />

### 半径

丸めの半径をピクセル単位で指定します．小数点以下2桁まで指定できます．

最小値は 0, 最大値は 500, 初期値は 32.

### ぼかし

境界にぼかしをかけられます．ぼかしの大きさをピクセル単位で指定します．

最小値は 0, 最大値は 500, 初期値は 0.

### 方式

角丸めの計算に使うアルゴリズムを選択します．

選択肢は[「縁取りσ」の「方式」](#方式)と同じです．

初期値は `2値化スムーズ`.

### α調整

[「縁取りσ」の「α調整」](#α調整)と同様，[「方式」](#方式-1)に関する調整をします．

最小値は 0, 最大値は 100, 初期値は 50.

### 縁の縮小

不透明ピクセルの境界に沿って，縁部分を削ることができます．削る幅をピクセル単位で指定します．

最小値は 0, 最大値は 500, 初期値は 0.

### サイズ固定

[「縁の縮小」](#縁の縮小)で削った幅の分だけ，オブジェクトのサイズを小さくするかどうかを指定します．

- OFF だとオブジェクトのサイズが「縁の縮小」の分だけ小さくなります．
- ON だとオブジェクトのサイズは変わりません．

ただし，[「透明度」](#透明度-1)が 100 未満の場合は常に ON 扱いです．

初期値は ON.

### 透明度

角丸めや[「縁の縮小」](#縁の縮小)で消去されるピクセルの透明度を % 単位で指定します．負の値も指定できます．

| 透明度 | 説明 |
|:---:| :--- |
| 正の値 | 本来なら消去されるピクセルがその透明度になります．100 % だと完全に消えます． |
| 0 | 何も変化しません． |
| 負の値 | 本来なら消去されるピクセルはそのままに，他のピクセルが透明になっていきます．-100 % だと削られる部分のピクセルだけが残ります． |

最小値は -100, 最大値は 100, 初期値は 100.

### 縦横比

角を丸める際の円に縦横比を持たせて，楕円形に丸めることができます．

% 単位で指定，0 で真円，正の値で縦長の楕円形，負の値で横長の楕円形になります．

最小値は -100, 最大値は 100, 初期値は 0.

### 膨らみ

[「縁取りσ」の「膨らみ」](#膨らみ)と同様に，スーパー楕円の形にできます．

最小値は -300, 最大値は 300, 初期値は 100.


##  アウトラインσ

オブジェクトの不透明ピクセルの境界に沿って，幅のあるラインを作成します．

「縁取りσ」がオブジェクトの装飾目的なのに対して，「アウトラインσ」はオブジェクトの輪郭に沿った別オブジェクトを作成することを目的としていて，ギャップの空いた縁取りや，元画像の内側に沿ってラインを引いたりと，多様な装飾ができます．

<img width="440" height="220" alt="ギャップのある縁取りや内側に引いたラインの例" src="https://github.com/user-attachments/assets/a4ffcfe3-f85d-4fd9-a5d4-b48b3b2d2f42" />

### 距離

ラインとオブジェクトの距離をピクセル単位で指定します．

正の値でオブジェクトの外側方向，負の値でオブジェクトの内側方向に位置取ります．

最小値は -500, 最大値は 500, 初期値は 10.

### ライン

ラインの幅をピクセル単位で指定します．[「距離」](#距離)で指定した位置がライン幅の片側で，もう片側の位置が決まります．

正の値でオブジェクトの外側方向，負の値でオブジェクトの内側方向にラインが描画されます．

負の方向に大きい値を指定すると，ラインの形ではなく塗りつぶし図形になります．

最小値は -4000, 最大値は 500, 初期値は 10.

- 便宜的に最小値は -4000 にしていますが，実際は -1000 未満の値だと常に塗りつぶしの形になります．

### ぼかし

アウトラインにぼかしをかけられます．ぼかしの大きさをピクセル単位で指定します．ただし，[「ライン」](#ライン)の (絶対値の) 半分が実効的な上限です．

最小値は 0, 最大値は 500, 初期値は 0.

### 縁色

アウトラインの色を指定します．

初期値は `ffffff` (白色).

### 方式

アウトラインの計算に使うアルゴリズムを選択します．

選択肢は[「縁取りσ」の「方式」](#方式)と同じです．

初期値は `2値化スムーズ`.

### α調整

[「縁取りσ」の「α調整」](#α調整)と同様，[「方式」](#方式-2)に関する調整をします．

最小値は 0, 最大値は 100, 初期値は 50.

### ライン配置

元画像と合成する際の前後関係を指定します．

| ライン配置 | 説明 |
|:---:|:---|
| `背面` | ラインを元画像の背面に配置します． |
| `前面` | ラインを元画像の前面に配置します． |

<img width="440" height="240" alt="ライン配置の表示例" src="https://github.com/user-attachments/assets/22da9e3c-6375-4b8e-ad2f-6502baa0e05b" />

初期値は `背面`.

### ライン透明度

ラインの透明度を % 単位で指定します．

最小値は 0, 最大値は 100, 初期値は 0.

### 内側透明度

ライン幅の内側の透明度を % 単位で指定します．100 % だとラインの内部に「穴」が空いたような見た目になり，0 % だとラインの内側も完全に塗りつぶされます ([「ライン」](#ライン)が -4000 の場合と同等).

最小値は 0, 最大値は 100, 初期値は 100.

### 元画像透明度

ラインと合成する際の元画像の透明度を % 単位で指定します．100 % だと元画像は表示されません．

最小値は 0, 最大値は 100, 初期値は 100.

### 距離縦横比

[「距離」](#距離)で指定するライン位置の計算で縦横比を持たせます．

% 単位で指定，0 で真円，正の値で縦長の楕円形，負の値で横長の楕円形になります．

最小値は -100, 最大値は 100, 初期値は 0.

### 凸半径

[「距離」](#距離)で指定するライン位置の，凸部分の最低保証の曲率半径です．この値が大きいほど，凸部分が緩やかになります．

最小値は 0, 最大値は 500, 初期値は 0.

### 凹半径

[「距離」](#距離)で指定するライン位置の，凹部分の最低保証の曲率半径です．この値が大きいほど，凹部分が緩やかになります．

最小値は 0, 最大値は 500, 初期値は 0.

### 距離膨らみ

[「距離」](#距離)で指定するライン位置を[「縁取りσ」の「膨らみ」](#膨らみ)と同様に，スーパー楕円の形にできます．

最小値は -300, 最大値は 300, 初期値は 100.

### ライン縦横比

ラインの線幅に縦横比を持たせて，楕円のラインを描くことができます．縦横に平たいラインになります．

<img width="360" height="180" alt="縦横に偏りを持たせたラインの例" src="https://github.com/user-attachments/assets/0d3c3a83-1b7d-41b3-b751-b1da007742d5" />

% 単位で指定，0 で真円，正の値で縦長の楕円形，負の値で横長の楕円形になります．

最小値は -100, 最大値は 100, 初期値は 0.

### ライン膨らみ

ラインの形を[「縁取りσ」の「膨らみ」](#膨らみ)と同様に，スーパー楕円の形にできます．

最小値は -300, 最大値は 300, 初期値は 100.

### 手順

[「凸半径」](#凸半径-1)と[「凹半径」](#凹半径-1)が両方とも正の場合のみ効果があります．

最低保証曲率半径を実現するために，外側縁取りや内側縁取りの手順を繰り返しますが，その適用順序によって結果が変わります．どういった手順で適用するかを指定します．

| 手順 | 説明 |
|:---:|:---|
| `外側優先` | [「距離」](#距離)が正の場合に効率的になる手順です．「内側 → 外側 → 内側」の順序で適用します． |
| `内側優先` | 「距離」が負の場合に効率的になる手順です．「外側 → 内側 → 外側」の順序で適用します． |
| `速い方` | 現在の「距離」に応じて上記2つを切り替えます． |

初期値は `外側優先`.

##  TIPS

1.  [「方式」](#方式)を選ぶ基準としては，以下のように選ぶと綺麗に見えることが多いです．

    - テキストに対しては `総和`.
    - その他図形等に対しては `2値化スムーズ`.

    また，2値化系は多重に縁取りをしていくと平坦な面ができやすくなるため，その場合も `総和` を利用すると綺麗に見えやすいです．

##  既知の問題

1.  標準の「縁取り」にあるような，パターン画像などの適用は現在できません．

##  改版履歴

- **v1.01 (for beta40a)** (2026-04-06)

  - 一部条件でエラー / クラッシュしていたのを修正．
  - 一部処理の最適化．

- **v1.00 (for beta39)** (2026-03-31)

  - 初版．


## ライセンス

このプログラムの利用・改変・再頒布等に関しては MIT ライセンスに従うものとします．

---

The MIT License (MIT)

Copyright (C) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

https://mit-license.org/


# Credits

##  AviUtl ExEdit2 SDK

http://spring-fragrance.mints.ne.jp/aviutl

```
---------------------------------
AviUtl ExEdit2 Plugin SDK License
---------------------------------

The MIT License

Copyright (c) 2025 Kenkun

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

# 連絡・バグ報告

- GitHub: https://github.com/sigma-axis
- Twitter: https://x.com/sigma_axis
- nicovideo: https://www.nicovideo.jp/user/51492481
- Misskey.io: https://misskey.io/@sigma_axis
- Bluesky: https://bsky.app/profile/sigma-axis.bsky.social
