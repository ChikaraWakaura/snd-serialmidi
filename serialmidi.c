/*
 *  Generic driver for serial MIDI adapters
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Chikara Wakaura <wakaurac@gmail.com>
 *
 *   2018/12/22 C.Wakaura
 *   This code is based on the code from ALSA 1.0.25, but heavily rewritten.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
/*
 *  The adaptor module parameter allows you to select:
 *  0 - Roland SoundCanvas (use outs paratemer to specify count of output ports)
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <sound/core.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>

#define SNDRV_SERIAL_MAX_OUTS       16 /* max 64, min 16 */
#define SNDRV_SERIAL_MAX_INS        16 /* max 64, min 16 */
#define TX_BUF_SIZE         4096
#define RX_BUF_SIZE         4096

#define SERIAL_ADAPTOR_SOUNDCANVAS  0  /* Roland Soundcanvas; F5 NN selects part */
#define SERIAL_ADAPTOR_MS124T       1  /* Midiator MS-124T */
#define SERIAL_ADAPTOR_MS124W_SA    2  /* Midiator MS-124W in S/A mode */
#define SERIAL_ADAPTOR_MS124W_MB    3  /* Midiator MS-124W in M/B mode */
#define SERIAL_ADAPTOR_MAX      SERIAL_ADAPTOR_MS124W_MB

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Serial MIDI");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;      /* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;       /* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE; /* Enable this card */
static char *sdev[SNDRV_CARDS] = {"/dev/ttyUSB0", [1 ... (SNDRV_CARDS - 1)] = ""}; /* serial device */
static int speed[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 38400}; /* 9600,19200,38400,57600,115200 */
static int adaptor[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = SERIAL_ADAPTOR_SOUNDCANVAS};
static int outs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};     /* 1 to 16 */
static int ins[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};  /* 1 to 16 */
static int devices[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};     /* 1 to 8 */
static bool handshake[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};     /* bool */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Serial MIDI.");
module_param_array(devices, int, NULL, 0444);
MODULE_PARM_DESC(devices, "Device count for Serial MIDI.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Serial MIDI.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Serial MIDI.");
module_param_array(sdev, charp, NULL, 0444);
MODULE_PARM_DESC(sdev, "Device file string for Serial device.");
module_param_array(speed, int, NULL, 0444);
MODULE_PARM_DESC(speed, "Speed in bauds.");
module_param_array(adaptor, int, NULL, 0444);
MODULE_PARM_DESC(adaptor, "Type of adaptor.");
module_param_array(outs, int, NULL, 0444);
MODULE_PARM_DESC(outs, "Number of MIDI outputs.");
module_param_array(ins, int, NULL, 0444);
MODULE_PARM_DESC(ins, "Number of MIDI inputs.");
module_param_array(handshake, bool, NULL, 0444);
MODULE_PARM_DESC(handshake, "Do handshaking.");

#define SERIAL_MODE_NOT_OPENED          (0)
#define SERIAL_MODE_BIT_INPUT           (1 << 0)
#define SERIAL_MODE_BIT_OUTPUT          (1 << 1)
#define SERIAL_MODE_BIT_INPUT_TRIGGERED     (1 << 2)
#define SERIAL_MODE_BIT_OUTPUT_TRIGGERED    (1 << 3)

typedef struct _snd_serialmidi {
    struct snd_card *card;
    char *sdev;         /* serial device name (e.g. /dev/ttyUSB0) */
    int dev_idx;
    unsigned int speed;     /* speed in bauds */
    unsigned int adaptor;       /* see SERIAL_ADAPTOR_ */
    unsigned long mode;     /* see SERIAL_MODE_* */
    unsigned int outs;      /* count of outputs */
    unsigned int ins;       /* count of inputs */
    int handshake;          /* handshake (RTS/CTS) */
    struct snd_rawmidi *rmidi;  /* rawmidi device */
    struct snd_rawmidi_substream *substream_input;
    struct snd_rawmidi_substream *substream_output;
    struct file *file;
    struct tty_struct *tty;
    struct mutex open_lock;
    struct mutex F5lock;
    struct task_struct *kthread_rx;
    int old_exclusive;
    int old_port_flags;
    unsigned char *tx_buf;
    unsigned char *rx_buf;
    int open_tty_call_cnt;
    int current_outport_no;     /* MIDI:F5 NN support */
} serialmidi_t;

static struct platform_device *devptrs[SNDRV_CARDS];

#ifdef CONFIG_SET_FS
static int ioctl_tty(struct file *file, unsigned int cmd, unsigned long arg)
{
    mm_segment_t fs;
    int retval;

    if (file->f_op == NULL)
        return -ENXIO;
    if (file->f_op->unlocked_ioctl == NULL)
        return -ENXIO;
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
        fs = get_fs();
        set_fs(KERNEL_DS);
    #else
        fs = force_uaccess_begin();    
    #endif
    retval = file->f_op->unlocked_ioctl(file, cmd, arg);
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
        set_fs(fs);
    #else
        force_uaccess_end(fs);
    #endif
    return retval;
}
#endif  // CONFIG_SET_FS

static int open_tty(serialmidi_t *serial, unsigned long mode)
{
    int retval = 0;
    struct tty_file_private *tty_fp;
    struct tty_struct *tty = NULL;
    struct ktermios old_termios, ntermios;
    struct tty_driver *driver;
    int ldisc;
    int speed, cflag, mstatus;

    mutex_lock(&serial->open_lock);
    if (serial->tty) {
        set_bit(mode, &serial->mode);
        goto __end;
    }
    if (IS_ERR(serial->file = filp_open(serial->sdev, O_RDWR | O_NONBLOCK | O_NOCTTY, 0))) {
        retval = PTR_ERR(serial->file);
        serial->file = NULL;
        goto __end;
    }

    tty_fp = (struct tty_file_private *)serial->file->private_data;
    if ( tty_fp )
        tty = tty_fp->tty;

    if (tty == NULL) {
        snd_printk(KERN_ERR "device %s has not valid tty\n", serial->sdev);
        retval = -EIO;
        goto __end;
    }

    driver = tty->driver;
    if (driver == NULL || driver->ops->set_termios == NULL) {
        snd_printk(KERN_ERR "tty %s has not set_termios\n", serial->sdev);
        retval = -EIO;
        goto __end;
    }
#ifdef CONFIG_HAVE_TTY_COUNT_ATOMIC
    if (atomic_read(&tty->count) > 1) {
#else
    if (tty->count > 1) {
#endif
        snd_printk(KERN_ERR "tty %s is already used\n", serial->sdev);
        retval = -EBUSY;
        goto __end;
    }

    /* select N_TTY line discipline (for sure) */
    ldisc = N_TTY;
#ifdef CONFIG_SET_FS
    if ((retval = ioctl_tty(serial->file, TIOCSETD, (unsigned long)&ldisc)) < 0) {
        snd_printk(KERN_ERR "TIOCSETD (N_TTY) failed for tty %s\n", serial->sdev);
        goto __end;
    }
#else
    if ((retval = tty_set_ldisc(tty, ldisc)) < 0) {
        snd_printk(KERN_ERR "tty_set_ldisc (N_TTY) failed for tty %s\n", serial->sdev);
        goto __end;
    }
#endif

    switch (serial->speed) {
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
    default:
        speed = B38400;
        break;
    case 57600:
        speed = B57600;
        break;
    case 115200:
        speed = B115200;
        break;
    }

    cflag = speed | CREAD | CS8 | HUPCL; // | CLOCAL;
    if (serial->handshake)
        cflag |= CRTSCTS;

    old_termios = tty->termios;
    ntermios = tty->termios;
    ntermios.c_lflag = NOFLSH;
    ntermios.c_iflag = IGNBRK | IGNPAR;
    ntermios.c_oflag = 0;
    ntermios.c_cflag = cflag;
    ntermios.c_cc[VEOL] = 0; /* '\r'; */
    ntermios.c_cc[VERASE] = 0;
    ntermios.c_cc[VKILL] = 0;
    ntermios.c_cc[VMIN] = 0;
    ntermios.c_cc[VTIME] = 0;
    tty_set_termios(tty, &ntermios);
    serial->tty = tty;

#ifdef CONFIG_SET_FS
    if ((retval = ioctl_tty(serial->file, TIOCMGET, (unsigned long)&mstatus)) < 0) {
        snd_printk(KERN_ERR "TIOCMGET failed for tty %s\n", serial->sdev);
        goto __end;
    }
#else
    mstatus = tty->ops->tiocmget(tty);
    if (mstatus < 0) {
        snd_printk(KERN_ERR "TIOCMGET failed for tty %s\n", serial->sdev);
        goto __end;
    }
#endif

    /* DTR , RTS , LE ON */
    mstatus |= TIOCM_DTR;
    mstatus |= TIOCM_RTS;
    mstatus |= TIOCM_LE;
    // mstatus |= TIOCM_CTS;

#ifdef CONFIG_SET_FS
    if ((retval = ioctl_tty(serial->file, TIOCMSET, (unsigned long)&mstatus)) < 0) {
        snd_printk(KERN_ERR "TIOCMSET failed for tty %s errno %d\n", serial->sdev, retval);
        goto __end;
    }
#else
    if ((retval = tty->ops->tiocmset(tty, mstatus, 0)) < 0) {
        snd_printk(KERN_ERR "TIOCMSET failed for tty %s errno %d\n", serial->sdev, retval);
        goto __end;
    }
#endif

    serial->old_port_flags = tty->port->flags;
    serial->old_exclusive = test_bit(TTY_EXCLUSIVE, &tty->flags);
    //tty->port->flags |= ASYNC_LOW_LATENCY;
    set_bit(TTY_EXCLUSIVE, &tty->flags);

    set_bit(mode, &serial->mode);
    retval = 0;

    snd_printk(KERN_INFO"snd-serialmidi opened %s\n", serial->sdev);
    
    __end:
        if (retval < 0) {
            if (serial->file) {
                filp_close(serial->file, NULL);
                serial->file = NULL;
            }
        } else {
            serial->open_tty_call_cnt++;
        }

    mutex_unlock(&serial->open_lock);
    return retval;
}

static int close_tty(serialmidi_t *serial, unsigned long mode)
{
    struct tty_struct *tty;

    mutex_lock(&serial->open_lock);
    clear_bit(mode, &serial->mode);
    if ( serial->mode != SERIAL_MODE_NOT_OPENED )
        goto __end;
    if ( serial->open_tty_call_cnt != 1 )
        goto __end;

    tty = serial->tty;
    if (tty) {
        tty->port->flags = serial->old_port_flags;
        if (serial->old_exclusive)
            set_bit(TTY_EXCLUSIVE, &tty->flags);
        else
            clear_bit(TTY_EXCLUSIVE, &tty->flags);
    }

    filp_close(serial->file, NULL);
    serial->tty = NULL;
    serial->file = NULL;

      __end:
    serial->open_tty_call_cnt--;
    mutex_unlock(&serial->open_lock);
    return 0;
}

static int kthread_rx_main( void *arg )
{
    serialmidi_t *serial = arg;
    struct tty_struct *tty;
    struct tty_ldisc *ldisc;
    unsigned char *rx_buf;
    void *cookie;
    ssize_t offset;
    ssize_t count;

    if ( serial == NULL )
        return 0;

    tty = serial->tty;
    rx_buf = serial->rx_buf;
    ldisc = tty->ldisc;
    
    if ( rx_buf == NULL || ldisc == NULL) {
        snd_printk(KERN_ERR "kthread_rx_main failed for tty %s\n", serial->sdev);
        return 0;
    }

    while ( !kthread_should_stop() )
    {
        if (test_bit(SERIAL_MODE_BIT_INPUT_TRIGGERED, &serial->mode)) {
            offset = 0;
            cookie = NULL;
            do {
                count = ldisc->ops->read( tty, serial->file, rx_buf, RX_BUF_SIZE, &cookie, offset);
                if ( count > 0) {
                    /* snd_printk(KERN_INFO "read for tty %s %ld\n", serial->sdev, count); */
                    offset += count;
                    if (snd_rawmidi_receive( serial->substream_input, rx_buf, count ) < 0) {
                        snd_printk(KERN_ERR "snd_rawmidi_receive failed for tty %s\n", serial->sdev);
                    }
                } else if (count < 0) {
                    if (count != -EAGAIN)
                        snd_printk(KERN_ERR "tty read failed for tty %s with code %ld\n", serial->sdev, count);
                    msleep(1);
                    if (count != -EAGAIN)
                        break;
                } else {
                    msleep(1);
                    break;
                }
            } while (cookie);
        }
        schedule();
    }
    serial->kthread_rx = NULL;      // FIXME: ??

    return 0;
}

static void tx_loop(serialmidi_t *serial)
{
    struct tty_struct *tty;
    struct tty_ldisc *ldisc;
    unsigned char *tx_buf;
    unsigned char midi_data[2];
    int count;

    tty = serial->tty;
    tx_buf = serial->tx_buf;
    ldisc = tty->ldisc;

    while ( 1 ) {
        count = snd_rawmidi_transmit_peek( serial->substream_output, tx_buf, TX_BUF_SIZE );
        if ( count <= 0 )
            break;

        if ( serial->adaptor == SERIAL_ADAPTOR_SOUNDCANVAS
          && serial->outs > 1 ) {
            midi_data[0] = 0x00;
            mutex_lock( &serial->F5lock);
            if ( serial->current_outport_no != serial->substream_output->number + 1 ) {
                serial->current_outport_no = serial->substream_output->number + 1;
                midi_data[0] = 0xF5;
                midi_data[1] = serial->current_outport_no;
            }
            mutex_unlock( &serial->F5lock );
            if ( midi_data[0] == 0xF5 )
                ldisc->ops->write( tty, serial->file, midi_data, 2 );
        }

        count = ldisc->ops->write( tty, serial->file, tx_buf, count );
        if ( count > 0 ) {
            snd_rawmidi_transmit_ack( serial->substream_output, count );
        }
    }
}

static void snd_serialmidi_output_trigger(struct snd_rawmidi_substream * substream, int up)
{
    serialmidi_t *serial = substream->rmidi->private_data;
    
    if (up) {
        set_bit(SERIAL_MODE_BIT_OUTPUT_TRIGGERED, &serial->mode);
        tx_loop(serial);
    } else {
        clear_bit(SERIAL_MODE_BIT_OUTPUT_TRIGGERED, &serial->mode);
    }
}

static void snd_serialmidi_input_trigger(struct snd_rawmidi_substream * substream, int up)
{
    serialmidi_t *serial = substream->rmidi->private_data;

    if (up) {
        set_bit(SERIAL_MODE_BIT_INPUT_TRIGGERED, &serial->mode);
    } else {
        clear_bit(SERIAL_MODE_BIT_INPUT_TRIGGERED, &serial->mode);
    }
}

static int snd_serialmidi_output_open(struct snd_rawmidi_substream * substream)
{
    serialmidi_t *serial = substream->rmidi->private_data;
    int err;

    if ((err = open_tty(serial, SERIAL_MODE_BIT_OUTPUT)) < 0)
        return err;
    serial->substream_output = substream;
    return 0;
}

static int snd_serialmidi_output_close(struct snd_rawmidi_substream * substream)
{
    serialmidi_t *serial = substream->rmidi->private_data;

    serial->substream_output = NULL;
    return close_tty(serial, SERIAL_MODE_BIT_OUTPUT);
}

static int snd_serialmidi_input_open(struct snd_rawmidi_substream * substream)
{
    serialmidi_t *serial = substream->rmidi->private_data;
    int err;

    if ((err = open_tty(serial, SERIAL_MODE_BIT_INPUT)) < 0)
        return err;
    serial->substream_input = substream;
    snd_printk(KERN_INFO "kthread_run for tty %s subtream:%d %s %d\n", 
            serial->sdev, substream->number, 
            serial->card->shortname, serial->dev_idx );
    serial->kthread_rx = kthread_run( kthread_rx_main, serial,
                      "%s %d", serial->card->shortname, serial->dev_idx );
    if ( serial->kthread_rx == NULL ) {
        snd_printk(KERN_ERR "kthread_run failed for tty %s\n", serial->sdev);
        return -EIO;
    }
    return 0;
}

static int snd_serialmidi_input_close(struct snd_rawmidi_substream * substream)
{
    serialmidi_t *serial = substream->rmidi->private_data;

    if ( serial->kthread_rx ) {
        kthread_stop( serial->kthread_rx );
        serial->kthread_rx = NULL;
    }

    serial->substream_input = NULL;
    return close_tty(serial, SERIAL_MODE_BIT_INPUT);
}

static struct snd_rawmidi_ops snd_serialmidi_output =
{
    .open =     snd_serialmidi_output_open,
    .close =    snd_serialmidi_output_close,
    .trigger =  snd_serialmidi_output_trigger,
};

static struct snd_rawmidi_ops snd_serialmidi_input =
{
    .open =     snd_serialmidi_input_open,
    .close =    snd_serialmidi_input_close,
    .trigger =  snd_serialmidi_input_trigger,
};

static int snd_serialmidi_free(serialmidi_t *serial)
{
    if (serial->sdev) {
        kfree(serial->sdev);
    }
    if (serial->tx_buf) {
        kfree(serial->tx_buf);
    }
    if (serial->rx_buf) {
        kfree(serial->rx_buf);
    }
    kfree(serial);
    return 0;
}

static int snd_serialmidi_dev_free(struct snd_device *device)
{
    serialmidi_t *serial = device->device_data;
    return snd_serialmidi_free(serial);
}

static int snd_serialmidi_rmidi(serialmidi_t *serial)
{
    struct snd_rawmidi *rrawmidi;
    int err;

    err = snd_rawmidi_new(serial->card, "Serial MIDI", serial->dev_idx,
              serial->outs, serial->ins, &rrawmidi);
    if ( err < 0)
        return err;
    snd_rawmidi_set_ops(rrawmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_serialmidi_input);
    snd_rawmidi_set_ops(rrawmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_serialmidi_output);
    snprintf(rrawmidi->name, sizeof(rrawmidi->name), "%s [%s] %d",
         serial->card->shortname, serial->sdev, serial->dev_idx);
    rrawmidi->info_flags = SNDRV_RAWMIDI_INFO_INPUT |
                   SNDRV_RAWMIDI_INFO_OUTPUT |
                   SNDRV_RAWMIDI_INFO_DUPLEX;
    rrawmidi->private_data = serial;
    serial->rmidi = rrawmidi;

    int icnt = rrawmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_count;
    int ocnt = rrawmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_count;
    snd_printk(KERN_INFO "streams for tty %s in:%d ou5:%d\n", serial->sdev, icnt, ocnt);

    return 0;
}

static int snd_serialmidi_create(struct snd_card *card, const char *sdev,
                    unsigned int speed, unsigned int adaptor,
                    unsigned int outs, unsigned int ins,
                    int idx, int hshake)
{
    static struct snd_device_ops ops = {
        .dev_free = snd_serialmidi_dev_free,
    };
    serialmidi_t *serial;
    int err;

    if ((serial = kzalloc(sizeof(*serial), GFP_KERNEL)) == NULL) {
        return -ENOMEM;
    }

    mutex_init(&serial->open_lock);
    mutex_init(&serial->F5lock);
    serial->card = card;
    serial->dev_idx = idx;
    serial->sdev = kstrdup(sdev, GFP_KERNEL);
    /* serial->tx_buf = kzalloc( sizeof( unsigned char ) * TX_BUF_SIZE, GFP_KERNEL ); */
    /* serial->rx_buf = kzalloc( sizeof( unsigned char ) * RX_BUF_SIZE, GFP_KERNEL ); */
    serial->tx_buf = kzalloc( sizeof( unsigned char ) * TX_BUF_SIZE, GFP_USER);
    serial->rx_buf = kzalloc( sizeof( unsigned char ) * RX_BUF_SIZE, GFP_USER );
    if (serial->sdev == NULL || serial->tx_buf == NULL || serial->rx_buf == NULL) {
        snd_serialmidi_free(serial);
        return -ENOMEM;
    }
    serial->adaptor = adaptor;
    serial->speed = speed;
    serial->outs = outs;
    serial->ins = ins;
    serial->handshake = hshake;
    serial->kthread_rx = NULL;
    serial->open_tty_call_cnt = 0;
    serial->current_outport_no = 0;

    /* Register device */
    if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, serial, &ops)) < 0) {
        snd_serialmidi_free(serial);
        return err;
    }

    if ((err = snd_serialmidi_rmidi(serial)) < 0) {
        snd_device_free(card, serial);
        return err;
    }
    
    return 0;
}

static int snd_card_serialmidi_probe(struct platform_device *devptr)
{
    struct snd_card *card;
    int err;
    int dev = devptr->id;

    switch (adaptor[dev]) {
    case SERIAL_ADAPTOR_SOUNDCANVAS:
        ins[dev] = 1;
        break;
    case SERIAL_ADAPTOR_MS124T:
    case SERIAL_ADAPTOR_MS124W_SA:
        outs[dev] = 1;
        ins[dev] = 1;
        break;
    case SERIAL_ADAPTOR_MS124W_MB:
        outs[dev] = 16;
        ins[dev] = 1;
        break;
    default:
        snd_printk(KERN_ERR
               "Adaptor type is out of range 0-%d (%d)\n",
               SERIAL_ADAPTOR_MAX, adaptor[dev]);
        return -ENODEV;
    }

    if (outs[dev] < 1 || outs[dev] > SNDRV_SERIAL_MAX_OUTS) {
        snd_printk(KERN_ERR
               "Count of outputs is out of range 1-%d (%d)\n",
               SERIAL_ADAPTOR_MAX, outs[dev]);
        return -ENODEV;
    }

    if (ins[dev] < 1 || ins[dev] > SNDRV_SERIAL_MAX_INS) {
        snd_printk(KERN_ERR
               "Count of inputs is out of range 1-%d (%d)\n",
               SNDRV_SERIAL_MAX_INS, ins[dev]);
        return -ENODEV;
    }

    err = snd_card_new(&devptr->dev, index[dev], id[dev], THIS_MODULE, 0, &card);
    if (err < 0)
        return err;

    strcpy(card->driver, "Serial MIDI");
    if (id[dev] && *id[dev])
        snprintf(card->shortname, sizeof(card->shortname), "%s", id[dev]);
    else
        strcpy(card->shortname, card->driver);

    printk(KERN_ERR "serialmidi: devices %d\n", devices[dev]);
    if (devices[dev] > 1) {
        int i, start_dev;
        char devname[32];
        /* assign multiple devices to a single card */
        if (devices[dev] > 8) {
            printk(KERN_ERR "serialmidi: invalid devices %d\n", devices[dev]);
            snd_card_free(card);
            return -EINVAL;
        }
        /* device name mangling */
        strncpy(devname, sdev[dev], sizeof(devname));
        devname[31] = 0;
        i = strlen(devname);
        if (i > 0) i--;
        if (devname[i] >= '0' && devname[i] <= '9') {
            for (; i > 0; i--)
                if (devname[i] < '0' || devname[i] > '9') {
                    i++;
                    break;
                }
            start_dev = simple_strtoul(devname + i, NULL, 0);
            devname[i] = 0;
        } else
            start_dev = 0;
        for (i = 0; i < devices[dev]; i++) {
            char devname2[33];
            sprintf(devname2, "%s%d", devname, start_dev + i);
            if ((err = snd_serialmidi_create(card, devname2, speed[dev],
                             adaptor[dev], outs[dev], ins[dev], i,
                             handshake[dev])) < 0) {
                snd_printk(KERN_ERR
                    "Failed to create %s (%d)\n", devname2, err);
                snd_card_free(card);
                return err;
            }
        }
    } else {
        if ((err = snd_serialmidi_create(card, sdev[dev], speed[dev],
                         adaptor[dev], outs[dev], ins[dev], 0,
                         handshake[dev])) < 0) {
            snd_printk(KERN_ERR
                "Failed to create %s (%d)\n", sdev[dev], err);
            snd_card_free(card);
            return err;
        }
    }

    sprintf(card->longname, "%s at %s", card->shortname, sdev[dev]);

    if ((err = snd_card_register(card)) < 0) {
        snd_printk(KERN_ERR "Failed to register card (%d)\n", err);
        snd_card_free(card);
        return err;
    }

    platform_set_drvdata(devptr, card);
    return 0;
}

static int snd_card_serialmidi_remove(struct platform_device *devptr)
{
        snd_card_free(platform_get_drvdata(devptr));
        return 0;
}

#define SND_CARD_SERIALMIDI_DRIVER       "snd_serialmidi"

static struct platform_driver snd_card_serialmidi_driver = {
        .probe          = snd_card_serialmidi_probe,
        .remove         = snd_card_serialmidi_remove,
        .driver         = {
                .name   = SND_CARD_SERIALMIDI_DRIVER,
        },
};

static void snd_card_serialmidi_unregister_all(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(devptrs); ++i)
        platform_device_unregister(devptrs[i]);
    platform_driver_unregister(&snd_card_serialmidi_driver);
}


static int __init alsa_card_serialmidi_init(void)
{
    int i, cards, err;

    if ((err = platform_driver_register(&snd_card_serialmidi_driver)) < 0)
        return err;

    cards = 0;
    for (i = 0; i < SNDRV_CARDS; i++) {
        struct platform_device *device;
        if (! enable[i])
            continue;
        device = platform_device_register_simple(SND_CARD_SERIALMIDI_DRIVER,
                             i, NULL, 0);
        if (IS_ERR(device))
            continue;
        if (!platform_get_drvdata(device)) {
            platform_device_unregister(device);
            continue;
        }
        devptrs[i] = device;
        cards++;
    }

    if (!cards) {
#ifdef MODULE
        printk(KERN_ERR "Serial MIDI device not found or device busy\n");
#endif
        snd_card_serialmidi_unregister_all();
        return -ENODEV;
    }
    return 0;
}

static void __exit alsa_card_serialmidi_exit(void)
{
    snd_card_serialmidi_unregister_all();
}

module_init(alsa_card_serialmidi_init)
module_exit(alsa_card_serialmidi_exit)
