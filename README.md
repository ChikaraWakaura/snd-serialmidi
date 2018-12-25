# 1. 概要

linux kernel 4.14.84 対応の /dev/tty* のシリアル MIDI カーネルドライバ(snd-serialmidi.ko)です。

# 2. 経緯

先日、年末大掃除前の取捨選択で貴重品と書かれたダンボール箱を押し入れから発見しました。  
なかに Roland SC-88Pro , YAMAHA MU15 , D-SUB9 MIDI 音源接続ケーブルが入っていました。

おおおお、懐かしい!!!  SC-88VL とか SC-55mkII とか YAMAHA MU-90 も買ったような記憶があるけど  
ヤフオクに行っただろうか...

Raspberry PI でもシリアル MIDI できるはずだし過去の記憶をたよりに /lib/module/ 以下をみても  
シリアル MIDI ができそうなのは snd-serial-u16550.ko しかない。しかも IBM PC/AT 互換機向けの ko が  
Raspberry PI に入ってるとは(笑)

/dev/tty* が利用可能な ko があったと思い ALSA 単独の最終版 [alsa-driver-1.0.25.tar.bz2](http://www.mirrorservice.org/sites/ftp.alsa-project.org/pub/driver/)
をみると drivers/ に  
serialmidi.c が、ぽつんと取り残されてる(笑)

えええええ(笑)　どうして?(笑)

そこから、じこじこ進めた結果がここです(笑)

# 3. ハード編

最重要

過去の 100 V 系ハード(自身の過去資産、中古取得品等)を通電確認するには細心の注意をはらって下さい。

発煙、発火の場合、落ち着いて直ちに電源を切りましょう。

ハードによっては内部の SRAM バックアップにリチウムコイン電池を使用している物もあります。  
リチウムコイン電池は長年の温湿度変化により液漏れで基板に短絡回路を自然生成する事もあります。  

いろいろな虫が住み着いて卵を生んで(以下省略ｗ)



さて Roland SC-88Pro と YAMAHA MU15 のそれぞれ全景と通電確認です。特に支障なくどちらも通電しました。
通電前に、ばらし清掃しましたが虫の卵、死骸等はありませんでした(爆笑)

![全景YAMAHA-MU15](/img/P_20181223_174535.jpg)
![全景YAMAHA-MU15-AC](/img/P_20181223_174714.jpg)
![通電YAMAHA-MU15](/img/P_20181223_174924.jpg)
![全面Roland-SC-88Pro](/img/P_20181223_175147.jpg)
![背面Roland-SC-88Pro](/img/P_20181223_175249.jpg)
![通電Roland-SC-88Pro](/img/P_20181223_175613.jpg)

正規の MIDI は 31.25 kbps です。クロック分周の影響で誤差 1 % に収まらない H/W が当時多々  
ありました。その対策に外部 MIDI 音源には 38.4 kbps (HOST PC-2)の機能が組み込まれて行きました。

HOST PC-1(NEC PC-9800シリーズ等) は 31.25 kbps。音源側のコネクタ形状を見てピンと来る方には  
分かると思います。当時の MAC は RS-422/425 でしたね。無理矢理逆差ししてピン潰し事件も多々ありました。  
と言うことで MIDI/PC-1/PC-2/MAC な HOST スイッチ実装機は RS-232C/422/425 が出来て 31.25 kbps と  
38.4 kbps ができるマルチシリアルコントローラが内蔵されていると考えられますね。

民生用途な USB シリアルコンバータは 38.4k bps が当然出来ますので外部 MIDI 音源を USB シリアルコンバータ  
経由する場合には HOST スイッチは PC-2 で利用します。

HOST スイッチが実装されていないもっと古いハードは DIN コネクタと SHARP PC-900 等で H/W 組んで 31.25 kbps と  
38.4 kbps の PIC ブリッジでも用意しましょう(笑)

小生は Raspberry PI 3 model B / FT232RL / pl2303 の組み合わせで実験しました。  

   $ uname -a  
   Linux raspi-002 4.14.84-v7+ #1169 SMP Thu Nov 29 16:20:43 GMT 2018 armv7l GNU/Linux  

![実験全景YAMAHA-MU15-FT232RL](/img/P_20181223_180350.jpg)

# 4. ソフト準備編

ALSA 単独の最終版 [alsa-driver-1.0.25.tar.bz2](http://www.mirrorservice.org/sites/ftp.alsa-project.org/pub/driver/) の doc/serialmidi.txt には linux kernel 統合されなかった経緯は何も書かれていませんでした。

残された drivers/serialmidi.c を読むしかありません...  

読んでて、もっとも気になったところが以下です。

    234         /* some magic here, we need own receive_buf */
    235         /* it would be probably better to create own line discipline */
    236         /* but this solution is sufficient at the time */
    237         tty->disc_data = serial;
    238         serial->old_receive_buf = tty->ldisc.receive_buf;
    239         tty->ldisc.receive_buf = ldisc_receive_buf;
    240         serial->old_write_wakeup = tty->ldisc.write_wakeup;
    241         tty->ldisc.write_wakeup = ldisc_write_wakeup;

tty 側の ldisc_data ポインタに自身のデータポインタを埋めて tty 側の receive_buf 関数へのポインタを自身側に差し替えて  
tty 側の write_wakeup 関数へのポインタを自身側に差し替えいるところです。 
こんな魔法なようなコードが(笑)

これは統合拒否される原因かもですね

じっくり読んだ結果以下の問題がわかりました。

(1) magic なコードは tty 側のアーキテクチャをハックしているので移植性が低い  
(2) MIDI : F5 NN の必要性は、当時ご理解されているみたい。実装はされていない  
(3) IN or OUT ポートを 1 以上にした場合に確実に close_tty() -> filp_close() で serial->file が NULL  
(4) 誤作動 SERIAL_MODE_BIT_* が実はビット値になっていない  
(5) カーネルメモリリーク or NULL 解放 428 行目 if (serial->sdev); となっている( ; が最後に書かれている)  

以下、各対策と改善方法です。

(1) MIDI IN(シリアル受信)は純粋に kthread と ldisc->ops->read() を使用し、  
    MIDI OUT(シリアル送信)は純粋に ldisc->ops->write() を使用  
(2) F5 NN 実装  
(3) 発生しないように改修  
(4) ビット値を正しく実装と副作用なし視点での動作確認  
(5) カーネルメモリリーク or NULL 解放を発生させないように改修  

意外と多いですがゼロからコードを書くわけでもないので頑張ってみました。  
永続的な利用でも無いですし Raspberry PI で ko のみメイクで  

loading out-of-tree module taints kernel.

↑は無視でいいかと。なんと言ってもレガシーすぎる外部 MIDI 音源ですし(笑)  

# 5. 動作確認

kernel ソースコード一式を必要とします。  

    $ sudo apt-get update
    $ sudo apt-get -y install ncurses-dev
    $ sudo apt-get -y install bc
    $ sudo wget https://raw.githubusercontent.com/notro/rpi-source/master/rpi-source -O /usr/bin/rpi-source
    $ sudo chmod +x /usr/bin/rpi-source
    $ sudo rpi-source --skip-gcc

メイクと実行  

    $ cd snd-serialmidi
    $ sudo make clean
    $ sudo make
    $ sudo make install

Raspberry PI 本体のシリアルは /dev/ttyAMA0 です。そのままでは TTL レベルです。  
TTL レベルと RS-232C の電気的変換が必要です。十分に出来る場合は Makefile を
以下のように変更して下さい。

元  

    install:
            cp snd-serialmidi.ko $(KERNEL_BASE)/sound/drivers
            depmod -a
            modprobe snd-serialmidi

変更後  

    install:
            cp snd-serialmidi.ko $(KERNEL_BASE)/sound/drivers
            depmod -a
            modprobe snd-serialmidi sdev=/dev/ttyAMA0

外部 MIDI 音源 HOST スイッチ PC-2 確認、外部 MIDI 音源シリアルケーブル接続確認、  
シリアルケーブルと USB シリアルコンバータの接続確認後、USB ポートへ差します。

dmesg または tail -5 /var/log/syslog で ko ロード確認が出来ます。  
taints kernel. は全無視で(笑)

    $ dmesg
    [  114.594564] snd_serialmidi: loading out-of-tree module taints kernel.

/dev/ttyUSB0 存在確認  

    $ ls -al /dev/ttyUSB0
    crw-rw---- 1 root dialout 188, 0 Dec 25 21:33 /dev/ttyUSB0

RTS(RS) / DTR(ER) の ON 確認は以下の画像でご了承下さい。
![RTS-DTR確認](/img/P_20181223_202618.jpg)

MIDI ポート確認  

    $ amidi -l
    Dir Device    Name
    IO  hw:0,0    Serial MIDI [/dev/ttyUSB0] 0

    $ arecordmidi -l
     Port    Client name                      Port name
     14:0    Midi Through                     Midi Through Port-0
     16:0    Serial MIDI                      Serial MIDI [/dev/ttyUSB0] 0

    $ aplaymidi -l
     Port    Client name                      Port name
     14:0    Midi Through                     Midi Through Port-0
     16:0    Serial MIDI                      Serial MIDI [/dev/ttyUSB0] 0

再生確認(MIDI OUT)  

    $ aplaymidi -p 16:0 hogehoge.midi

記録確認(MIDI IN)  

    $ amidi -d -p hw:0,0

上記待機状態になったら MIDI 機器からシステムエクスクルーシブダンプでも良いですし  
MIDI 音源経由で鍵盤を押して結果が表示されるか確認出来ます。

# 6. 単一外部 MIDI 音源 A 側 / B 側確認

外部 MIDI 音源には 16 トラック(orパート)を 2 セット持っているハードもありました。  
外部 MIDI 音源の ALL ボタンを押すと液晶表示に横線が入り上 B 側 / 下 A 側と見ていました。

背面 DIN コネクタの MIDI IN A/B と連動してます。

これをシリアルで制御する手段として F5 NN が規定されたと記憶しています。

F5 01 後の全データは A 側を示し F5 02 後の全データは B 側という制御だったと思います。  
うろ覚えです。間違っていたらご指摘下さい。

ロードされている ko があればアンロードします。  

    $ sudo rmmod snd-serialmidi

MIDI OUT を 2 つにしてロードします。  

    $ sudo modprobe snd-serialmidi outs=2

MIDI OUT が 2 つに増えます。  

    $ aplaymidi -l
    Port    Client name                      Port name
     14:0    Midi Through                     Midi Through Port-0
     16:0    Serial MIDI                      Serial MIDI [/dev/ttyUSB0] 0-0
     16:1    Serial MIDI                      Serial MIDI [/dev/ttyUSB0] 0-1

外部 MIDI 音源 A 側へ再生は以下です。  

    $ playmidi -p 16:0 hogehoge.mid

外部 MIDI 音源 B 側へ再生は以下です。  

    $ playmidi -p 16:1 hogehoge.mid

シーケンサソフトで 1 ～ 16 トラック(orパート)と 17 ～ 32 トラック(orパート)を  
個別に MIDI OUT ポート出力定義できる場合は 32 トラック(orパート)再生が可能になります。

記憶にある rosegarden は出来なかったような...　確認不足しています(笑)

# 7. 応用編

[raveloxmidi](https://github.com/ravelox/pimidi/tree/master/raveloxmidi) & [rtpMIDI](https://www.tobias-erichsen.de/software/rtpmidi.html) 利用の場合は以下のようになります。

    $ amidi -l
    Dir Device    Name
    IO  hw:0,0    Serial MIDI [/dev/ttyUSB0] 0

以下のようなファイルを ~/.config に準備します。

    $ cat ~/.config/raveloxmidi-rsmidi.conf
    service.name = serial-midi
    file_mode = 0666
    inbound_midi = /dev/null
    alsa.input_device = hw:0,0,0
    alsa.output_device = hw:0,0,0

raveloxmidi ファイル指定実行

    $ raveloxmidi -N -c ~/.config/raveloxmidi-rsmidi.conf

これで [rtpMIDI](https://www.tobias-erichsen.de/software/rtpmidi.html) が動作する環境より任意の MIDI 再生ソフトからの MIDI 再生が可能になり Raspberry PI 経由で  
外部 MIDI 音源へ MIDI 出力可能となります。

残念なことに [rtpMIDI](https://www.tobias-erichsen.de/software/rtpmidi.html) は MIDI IN データをアプリケーションへ通知しなかったと思います。  
受けてるけどアプリ側へ渡すロジックを実装忘れまたは事情により実装したくないだったような。  
うろ覚えです。間違っていたらご指摘下さい。

# 8. その他

root ユーザの直下にインストールされている kernel ソースコード一式は必要に応じて削除して下さい。  

    $ su -
    # rm -fr linux
    # rm -f linux*.tar.gz

久しぶりに聞いた外部 MIDI 音源での演奏は、とても良いですね。  
いろいろと思い出されます(YUI ネットとか PC-VAN sig とか FMIDI* とか compuserve とか)

以上です。
