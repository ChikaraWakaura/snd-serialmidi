# 1. Overview

It is a serial MIDI kernel driver (snd-serialmidi.ko) of /dev/tty* compatible with linux kernel 4.14.84. 

# 2. Circumstances

The other day, I found a corrugated card box written as valuables from pushing in by choosing before the large cleaning at the end of 2018.
Inside was Roland SC-88Pro, Yamaha MU15, D-SUB 9 MIDI sound source connection cable.

Oh Oh, nostalgic !! There is a memory that I bought SC-88VL, SC-55mkII or YAMAHA MU-90, but I wonder if I went to Yahoo! Au ...

Raspberry PI should be able to do serial MIDI, and it is only snd-serial-u16550.ko that can make serial MIDI even if you look at /lib/module/ or less from past memory. Moreover, ko for IBM PC/AT compatible machine What is in Raspberry PI (laugh)

I thought that there was an available ko for /dev/tty* and looking at the final version of ALSA alone [alsa-driver-1.0.25.tar.bz2](http://www.mirrorservice.org/sites/ftp.alsa-project.org/pub/driver/), it's in drivers/serialmidi.c has been left behind poorly (lol)

Eh yeah yeah (lol) Why? (Laugh)

From there, the result that we walked forward is here (laugh) 

# 3. Hard section

most important

Please pay close attention to confirm the energization of past 100/220 V type hard (own past assets, second hand acquired etc).

In case of smoke or ignition, let's calm down and immediately turn off the power.

Some hardware uses lithium coin batteries for internal SRAM backup.
Lithium coin batteries may spontaneously generate a short circuit on the board due to leakage due to long-term change in temperature and humidity.

Various insects have settled and bred eggs (hereinafter abbreviated w)

Well Roland SC-88 Pro and YAMAHA MU15 each have full view and electricity conduction check. Especially power supply did not interfere either way.
Before energizing, we cleaned up the bulk, but there were no insect eggs, carcasses etc (laughter) 

![全景YAMAHA-MU15](/img/P_20181223_174535.jpg)
![全景YAMAHA-MU15-AC](/img/P_20181223_174714.jpg)
![通電YAMAHA-MU15](/img/P_20181223_174924.jpg)
![背面YAMAHA-MU15](/img/P_20181223_175031.jpg)
![全面Roland-SC-88Pro](/img/P_20181223_175147.jpg)
![背面Roland-SC-88Pro](/img/P_20181223_175249.jpg)
![通電Roland-SC-88Pro](/img/P_20181223_175613.jpg)

Regular MIDI is 31.25 kbps. At the time there were many H/W which did not fit within 1% of error due to clock division. To that effect, the function of 38.4 kbps (HOST PC-2) was installed in the external MIDI sound source.

HOST PC-1 (NEC PC-9800 series etc.) is 31.25 kbps. I think that it is understood to the person who comes with the pin looking at the shape of the connector on the sound source side. At that time the MAC was RS-422/425. There was also a lot of incidents involving crushing pins by forcing them backwards.
By saying that MIDI/PC-1/PC-2/MAC HOST switch mounter is RS-232C/422/425 made 31.25 kbps with It seems that a multi-serial controller capable of 38.4 kbps is built in.

For consumer-oriented USB serial converter, 38.4 k bps is of course possible, so when using an external MIDI sound source via a USB serial converter, use the HOST switch with PC-2.

HOST switch is not installed Older hardware is 31.25 kbps with H/W combination with DIN connector and SHARP PC-900 etc.
Let's prepare even 38.4 kbps PIC bridge (lol)

I experimented with Raspberry PI 3 model B/FT232RL/pl2303 combination.

   $ uname -a  
   Linux raspi-002 4.14.84-v7+ #1169 SMP Thu Nov 29 16:20:43 GMT 2018 armv7l GNU/Linux  

![実験全景YAMAHA-MU15-FT232RL](/img/P_20181223_180350.jpg)

# 4. Software preparation

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

# 5. Operation check

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

    $ aplaymidi -p 16:0 hogehoge.mid

記録確認(MIDI IN)  

    $ amidi -d -p hw:0,0

上記待機状態になったら MIDI 機器からシステムエクスクルーシブダンプでも良いですし  
MIDI 音源経由で鍵盤を押して結果が表示されるか確認出来ます。

# 6. Single external MIDI sound source A side / B side confirmation

外部 MIDI 音源には 16 トラック(orパート)を 2 セット持っているハードもありました。  
外部 MIDI 音源の ALL ボタンを押すと液晶表示に横線が入り上 B 側 / 下 A 側と見ていました。

![ALL表示](/img/P_20190105_111236.jpg)

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

以下、再生画像です。曲名は放置で(笑)
![A側再生](/img/P_20190105_112432.jpg)

外部 MIDI 音源 B 側へ再生は以下です。  

    $ playmidi -p 16:1 hogehoge.mid

以下、再生画像です。曲名は放置で(笑)
![B側再生](/img/P_20190105_112654.jpg)

シーケンサソフトで 1 ～ 16 トラック(orパート)と 17 ～ 32 トラック(orパート)を  
個別に MIDI OUT ポート出力定義できる場合は 32 トラック(orパート)再生が可能になります。

記憶にある rosegarden は出来なかったような...　確認不足しています(笑)

# 7. Advanced version

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

# 8. Other

root ユーザの直下にインストールされている kernel ソースコード一式は必要に応じて削除して下さい。  

    $ su -
    # rm -fr linux
    # rm -f linux*.tar.gz

久しぶりに聞いた外部 MIDI 音源での演奏は、とても良いですね。  
いろいろと思い出されます(YUI ネットとか PC-VAN sig とか FMIDI* とか compuserve とか)

# 9. Revision

2018/12/29 MIDI IN kthread が CPU 100%  
この不具合改修として ldisc->ops->read() が 0 以下の場合 msleep(1) (1msec寝る) としました(笑)  
正規な流れは O_NONBLOCK なし VMIN = 1 で filp_open 中の TTY へ signal 配送で ldisc->ops->read()  
抜けと思いますがカーネルモジュール内で別カーネルモジュールへの signal 配送手段が分かりません(泣)  
ご存知の方は、ぜひ御教授または Pull Req をお願い致します。  

以上です。
