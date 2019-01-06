# 1. Overview

It is a serial MIDI kernel driver (snd-serialmidi.ko) of /dev/tty* compatible with linux kernel 4.14.84. 

# 2. History of things

The other day, I found a corrugated card box written as valuables from pushing in by choosing before the large cleaning at the end of 2018.
Inside was Roland SC-88Pro, Yamaha MU15, D-SUB 9 MIDI sound source connection cable.

Oh Oh, nostalgic !! There is a memory that I bought SC-88VL, SC-55mkII or YAMAHA MU-90, but I wonder if I went to Yahoo! Au ...

Raspberry PI should be able to do serial MIDI, and it is only snd-serial-u16550.ko that can make serial MIDI even if you look at /lib/module/ or less from past memory. Moreover, ko for IBM PC/AT compatible machine What is in Raspberry PI (laugh)

I thought that there was an available ko for /dev/tty* and looking at the final version of ALSA alone [alsa-driver-1.0.25.tar.bz2](http://www.mirrorservice.org/sites/ftp.alsa-project.org/pub/driver/), it's in drivers/serialmidi.c has been left behind poorly (lol)

Eh yeah yeah (lol) Why? (lol)

From there, the result that we walked forward is here (lol) 

# 3. Section of Hardware

most important

Please pay close attention to confirm the energization of past 100/220 V type hard (own past assets, second hand acquired etc).

In case of smoke or ignition, let's calm down and immediately turn off the power.

Some hardware uses lithium coin batteries for internal SRAM backup.
Lithium coin batteries may spontaneously generate a short circuit on the board due to leakage due to long-term change in temperature and humidity.

Various insects have settled and bred eggs... (lol)

Well Roland SC-88 Pro and YAMAHA MU15 each have full view and electricity conduction check. Especially power supply did not interfere either way.
I cleaned up, but there were no insect eggs, dead bodies.

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

# 4. Section of Software preparation

In the final version of ALSA alone, doc/serialmidi.txt of [alsa-driver-1.0.25.tar.bz2](http://www.mirrorservice.org/sites/ftp.alsa-project.org/pub/driver/) had no written description of how linux kernel was not integrated.

I have no choice but to read the remaining drivers/serialmidi.c ...

The place I read most, which I read, is below.

    234         /* some magic here, we need own receive_buf */
    235         /* it would be probably better to create own line discipline */
    236         /* but this solution is sufficient at the time */
    237         tty->disc_data = serial;
    238         serial->old_receive_buf = tty->ldisc.receive_buf;
    239         tty->ldisc.receive_buf = ldisc_receive_buf;
    240         serial->old_write_wakeup = tty->ldisc.write_wakeup;
    241         tty->ldisc.write_wakeup = ldisc_write_wakeup;

fills its own data pointer in the ldisc_data pointer on the tty side and replaces the pointer to the receive_buf function on the tty side to itself
We are replacing the pointer to the write_wakeup function on the tty side to itself. Such a magical code!

This may be the cause of unification refusal.

I found the following problem as a result of thorough reading.

(1) Portability is low because magic code hacks architecture on tty side.  
(2) The necessity of F5 NN seems to be understood at that time. It is not implemented.  
(3) When IN or OUT port is set to 1 or more certainly ensure that close_tty() ->filp_close() causes serial->file to be NULL  
(4) Malfunction SERIAL_MODE_BIT_ * is not actually a bit value.  
(5) kernel memory leak or NULL release line 428 if (serial->sdev); (it is written last)  

Below, each measure and improvement method.

(1) MIDI IN (serial receive) uses purely kthread and ldisc->ops->read()  
MIDI OUT (serial transmission) uses purely ldisc->ops->write()  
(2) F5 NN implementation  
(3) Refurbishment not to occur  
(4) Bit value correctly Implementation and operation confirmation with no side effects  
(5) Refurbish not to cause kernel memory leak or NULL release  

Although it is surprisingly many, I tried hard because I do not write codes from scratch.
It is not even a permanent use, and Raspberry PI makes ko only with makeup

    loading out-of-tree module taints kernel.

↑ can be ignored. It is an external MIDI sound source that is too legacy to say anything.

# 5. Operation check

I need a kernel source code set.

    $ sudo apt-get update
    $ sudo apt-get -y install ncurses-dev
    $ sudo apt-get -y install bc
    $ sudo wget https://raw.githubusercontent.com/notro/rpi-source/master/rpi-source -O /usr/bin/rpi-source
    $ sudo chmod +x /usr/bin/rpi-source
    $ sudo rpi-source --skip-gcc

Make and run  

    $ cd snd-serialmidi
    $ sudo make clean
    $ sudo make
    $ sudo make install

The serial of the Raspberry PI is /dev/ttyAMA0. It is the TTL level as it is.
Electrical conversion of TTL level and RS-232C is necessary. Please change Makefile as follows if it can do enough.

original.

    install:
            cp snd-serialmidi.ko $(KERNEL_BASE)/sound/drivers
            depmod -a
            modprobe snd-serialmidi

After change.

    install:
            cp snd-serialmidi.ko $(KERNEL_BASE)/sound/drivers
            depmod -a
            modprobe snd-serialmidi sdev=/dev/ttyAMA0

External MIDI sound source HOST switch PC-2 confirmation, external MIDI sound source Serial cable connection check,
After confirming the connection of the serial cable and the USB serial converter, plug it into the USB port.

You can check ko load in dmesg or tail -5 /var/log/syslog.  
Taints kernel. is all ignorant. (lol)

    $ dmesg
    [  114.594564] snd_serialmidi: loading out-of-tree module taints kernel.

/dev/ttyUSB0 existence confirmation.

    $ ls -al /dev/ttyUSB0
    crw-rw---- 1 root dialout 188, 0 Dec 25 21:33 /dev/ttyUSB0

Please confirm RTS (RS) / DTR (ER) ON check in the image below.
![RTS-DTR確認](/img/P_20181223_202618.jpg)

MIDI port confirmation.

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

Playback(MIDI OUT) confirmation.

    $ aplaymidi -p 16:0 hogehoge.mid

Record(MIDI IN) confirmation.

    $ amidi -d -p hw:0,0

When it comes to the standby state, you can use a system exclusive dump from a MIDI device.
You can check whether the result is displayed by pressing the keyboard via MIDI sound source.

# 6. Single external MIDI sound source A side / B side confirmation

There were also hards that have 2 sets of 16 tracks (or parts) as external MIDI sound sources.  
When you pressed the ALL button of the external MIDI sound source, the horizontal line entered the liquid crystal display and I saw it as B side / lower A side.  

![ALL表示](/img/P_20190105_111236.jpg)

It is linked with MIDI IN A/B on the rear DIN connector.

I remember that F5 NN was specified as a means to control this serially.

I think that all data after F5 01 was control on A side and all data after F5 02 was control on B side.  
It is foolish. Please point out if it is wrong.

Unload the loaded ko if it exists.

    $ sudo rmmod snd-serialmidi

Load it with 2 MIDI OUT.

    $ sudo modprobe snd-serialmidi outs=2

MIDI OUT will be increased to 2.

    $ aplaymidi -l
    Port    Client name                      Port name
     14:0    Midi Through                     Midi Through Port-0
     16:0    Serial MIDI                      Serial MIDI [/dev/ttyUSB0] 0-0
     16:1    Serial MIDI                      Serial MIDI [/dev/ttyUSB0] 0-1

Playback to the external MIDI sound source A side is below.  

    $ playmidi -p 16:0 hogehoge.mid

Below is the playback image. 
![A側再生](/img/P_20190105_112432.jpg)

Playback to the external MIDI sound source B side is below.  

    $ playmidi -p 16:1 hogehoge.mid

Below is the playback image. 
![B側再生](/img/P_20190105_112654.jpg)

If you can define MIDI OUT port output separately for 1 to 16 tracks (or parts) and 17 to 32 tracks (or parts) with the sequencer software, 32 tracks (or part) playback becomes possible.

It seems like rosegarden in memory could not be done... lacking confirmation (lol) 

# 7. take advantage of

In case of using [raveloxmidi](https://github.com/ravelox/pimidi/tree/master/raveloxmidi) & [rtpMIDI](https://www.tobias-erichsen.de/software/rtpmidi.html) , it becomes as follows.  

    $ amidi -l
    Dir Device    Name
    IO  hw:0,0    Serial MIDI [/dev/ttyUSB0] 0

Prepare the following file in ~ / .config.  

    $ cat ~/.config/raveloxmidi-rsmidi.conf
    service.name = serial-midi
    file_mode = 0666
    inbound_midi = /dev/null
    alsa.input_device = hw:0,0,0
    alsa.output_device = hw:0,0,0

Run designation of raveloxmidi file  

    $ raveloxmidi -N -c ~/.config/raveloxmidi-rsmidi.conf

With this, MIDI playback from any MIDI playback software becomes possible from the environment where [rtpMIDI](https://www.tobias-erichsen.de/software/rtpmidi.html) operates, and it becomes MIDI output to the external MIDI sound source via Raspberry PI.

Unfortunately I think that [rtpMIDI](https://www.tobias-erichsen.de/software/rtpmidi.html) did not notify MIDI IN data to the application.  
I'm receiving it but I do not want to implement the logic that I handed over to the application side due to forgetting implementation or circumstances.  
It is foolish. Please point out if it is wrong.

# 8. Other

Delete the kernel source code set installed directly under the root user as necessary.

    $ su -
    # rm -fr linux
    # rm -f linux*.tar.gz

The performance with an external MIDI sound source that I heard after a long time is very good, is not it?  
I remember many things (YUI net, PC-VAN sig, FMIDI * or compuserve)  

# 9. Revision

Dec/29/2018 MIDI IN kthread is CPU 100%  
As this bug repair msleep(1) (sleep 1 msec) if ldisc->ops->read() is less than 0.  
Regular flow is O_NONBLOCK Without VMIN = 1 to TTY in filp_open ldisc->ops->read() with signal delivery I think that it is missing but I do not know the signal delivery method to another kernel module in the kernel module.  
Those who knows, please give me Professor or Pull Req.

that's all.
