# comm_jsonrpc
comm_jsonrpc は，2台の ESP32 の可視光通信における，データのビット誤り率を測定するための実験用ツールである．本ツールは，可視光通信の性能評価を目的としたサーバ・クライアント型の測定環境を提供する．

# 背景
以前の可視光通信の実験環境では，ビット誤り率を測定する際に以下の課題があった．
+ 可視光通信における役割(送信・受信)や通信条件を ESP32 に書き込む必要があり，条件を変更するたびにビルド・フラッシュの必要があった
+ 送信データおよび受信データを確認するためには，ESP32 が利用するデバイスファイルからデータを確認する必要があった

本ツールでは，サーバプログラムを利用し，サーバプログラムと ESP32 が通信条件やデータに関するやりとりを行う仕組みを取っている．これにより以下が可能となる．
+ サーバプログラムから ESP32 に役割と通信条件を渡すことで，条件変更時にもビルド・フラッシュが不要となり，柔軟に実験を行える
+ 送信データおよび受信データを ESP32 からサーバに送信することで，
サーバ上でデータを確認でき，デバイスファイルを直接確認する必要がなくなる

# アーキテクチャ

# 処理フロー

# Requirements

# Setup
1. ダウンロードする
  ```bash
  $ git clone git@github.com:bear-KU/VLC.git
  ```

# Launch
## 事前準備
1. server.js を開き，Wi-Fi に関する箇所を書き換える
  ```config.h
  // Configuration
  const HOST = "192.168.1.63"; // サーバを動かすマシン の IP アドレス
  const PORT = 61001;          // ポート
  ```

2. config.h を作成する
  ```bash
  $ cd /your/path/to/directory/VLC/vlf_error_rate/comm_jsonrpc/client2
  $ cp config_example.h config.h 
  ```
3. config.h を開き， Wi-Fi に関する箇所を書き換える
  ```config.h
  // WiFi configuration
  const char *ssid = "exampleSSID";
  const char *password = "examplePassword";

  // Server configuration
  const char *serverIP = "123.456.789.012"; // サーバを動かすマシン の IP アドレス
  const int serverPort = 12345; // ポート
  ```
4. Makefile を開き，ESP32 が接続しているポート名と LED が利用する GPIO のピンを書き換える
  ```Makefile
  // 他のマイコンボードを用いる場合は，以下に追加する
  ifeq ($(BOARD), esp32s3)
    FQBN = esp32:esp32:esp32s3
    PORT = /dev/ueda_ESP32S3
    BAUDRATE = 115200
    LED_PIN = 4
  else ifeq ($(BOARD), esp32)
    FQBN = esp32:esp32:esp32
    PORT = /dev/ueda_ESP32
    BAUDRATE = 115200
    LED_PIN = 32
  else
    $(error Unknown BOARD_TYPE specified: $(BOARD))
  endif
  ```

## サーバプログラムの起動
1. サーバプログラムを起動する．
   ```bash
   $ node js_jsonrpc4 2 32 1000
   ```
   引数を，データ内容，ペイロードサイズ(bit)，基準時間(μs)の順に指定する．

   データ内容は以下の0，1，2から指定する．
   | 通番 | パラメータ | データ内容 |
   | ---: | ---: | :--- |
   | 1 | 0 | 0だけで構成されるデータ |
   | 2 | 1 | 1だけで構成されるデータ |
   | 3 | 2 | 0と1からなるランダムなデータ |

2. 可視光通信の結果がターミナル上に出力される

## ESP32 の起動
1. 2台の ESP32 にビルド，フラッシュする
   ```bash
   $ make BOARD=esp32s3
   ```
   BOARD には フラッシュ先のボード名を与える
2. 2台の ESP32 がサーバに接続すると可視光通信が開始する
3. 必要であれば，デバイスファイルから ESP32 の出力を確認する
   ```bash
   $ make screen BOARD=esp32s3
   ```

## デモ
