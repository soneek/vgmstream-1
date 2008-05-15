#include "meta.h"
#include "../coding/coding.h"
#include "../util.h"

/* If these variables are packed properly in the struct (one after another)
 * then this is actually how they are laid out in the file, albeit big-endian */

struct dsp_header {
    uint32_t sample_count;
    uint32_t nibble_count;
    uint32_t sample_rate;
    uint16_t loop_flag;
    uint16_t format;
    uint32_t loop_start_offset;
    uint32_t loop_end_offset;
    uint32_t ca;
    int16_t coef[16]; /* really 8x2 */
    uint16_t gain;
    uint16_t initial_ps;
    int16_t initial_hist1;
    int16_t initial_hist2;
    uint16_t loop_ps;
    int16_t loop_hist1;
    int16_t loop_hist2;
};

/* nonzero on failure */
static int read_dsp_header(struct dsp_header *header, off_t offset, STREAMFILE *file) {
    int i;
    uint8_t buf[0x4a]; /* usually padded out to 0x60 */
    if (read_streamfile(buf, offset, 0x4a, file) != 0x4a) return 1;

    header->sample_count =
        get_32bitBE(buf+0x00);
    header->nibble_count =
        get_32bitBE(buf+0x04);
    header->sample_rate =
        get_32bitBE(buf+0x08);
    header->loop_flag =
        get_16bitBE(buf+0x0c);
    header->format =
        get_16bitBE(buf+0x0e);
    header->loop_start_offset =
        get_32bitBE(buf+0x10);
    header->loop_end_offset =
        get_32bitBE(buf+0x14);
    header->ca =
        get_32bitBE(buf+0x18);
    for (i=0; i < 16; i++)
        header->coef[i] =
        get_16bitBE(buf+0x1c+i*2);
    header->gain =
        get_16bitBE(buf+0x3c);
    header->initial_ps =
        get_16bitBE(buf+0x3e);
    header->initial_hist1 =
        get_16bitBE(buf+0x40);
    header->initial_hist2 =
        get_16bitBE(buf+0x42);
    header->loop_ps =
        get_16bitBE(buf+0x44);
    header->loop_hist1 =
        get_16bitBE(buf+0x46);
    header->loop_hist2 =
        get_16bitBE(buf+0x48);

    return 0;
}

/* the standard .dsp, as generated by DSPADPCM.exe */

VGMSTREAM * init_vgmstream_ngc_dsp_std(const char * const filename) {
    VGMSTREAM * vgmstream = NULL;
    STREAMFILE * infile = NULL;

    struct dsp_header header;
    const off_t start_offset = 0x60;
    int i;

    /* check extension, case insensitive */
    if (strcasecmp("dsp",filename_extension(filename))) goto fail;

    /* try to open the file for header reading */
    infile = open_streamfile(filename);
    if (!infile) goto fail;

    if (read_dsp_header(&header, 0, infile)) goto fail;

    /* check initial predictor/scale */
    if (header.initial_ps != (uint8_t)read_8bit(start_offset,infile))
        goto fail;

    /* check type==0 and gain==0 */
    if (header.format || header.gain)
        goto fail;

    /* Check for a matching second header. If we find one and it checks
     * out thoroughly, we're probably not dealing with a genuine mono DSP.
     * In many cases these will pass all the other checks, including the
     * predictor/scale check if the first byte is 0 */
    {
        struct dsp_header header2;

        read_dsp_header(&header2, 0x60, infile);

        if (header.sample_count == header2.sample_count &&
            header.nibble_count == header2.nibble_count &&
            header.sample_rate == header2.sample_rate &&
            header.loop_flag == header2.loop_flag) goto fail;
    }
        
    if (header.loop_flag) {
        off_t loop_off;
        /* check loop predictor/scale */
        loop_off = header.loop_start_offset/16*8;
        if (header.loop_ps != (uint8_t)read_8bit(start_offset+loop_off,infile))
            goto fail;
    }

    /* compare num_samples with nibble count */
    /*
    fprintf(stderr,"num samples (literal): %d\n",read_32bitBE(0,infile));
    fprintf(stderr,"num samples (nibbles): %d\n",dsp_nibbles_to_samples(read_32bitBE(4,infile)));
    */

    /* build the VGMSTREAM */


    vgmstream = allocate_vgmstream(1,header.loop_flag);
    if (!vgmstream) goto fail;

    /* fill in the vital statistics */
    vgmstream->num_samples = header.sample_count;
    vgmstream->sample_rate = header.sample_rate;

    vgmstream->loop_start_sample = dsp_nibbles_to_samples(
            header.loop_start_offset);
    vgmstream->loop_end_sample =  dsp_nibbles_to_samples(
            header.loop_end_offset)+1;

    /* don't know why, but it does happen*/
    if (vgmstream->loop_end_sample > vgmstream->num_samples)
        vgmstream->loop_end_sample = vgmstream->num_samples;

    vgmstream->coding_type = coding_NGC_DSP;
    vgmstream->layout_type = layout_none;
    vgmstream->meta_type = meta_DSP_STD;

    /* coeffs */
    for (i=0;i<16;i++)
        vgmstream->ch[0].adpcm_coef[i] = header.coef[i];
    
    /* initial history */
    /* always 0 that I've ever seen, but for completeness... */
    vgmstream->ch[0].adpcm_history1_16 = header.initial_hist1;
    vgmstream->ch[0].adpcm_history2_16 = header.initial_hist2;

    close_streamfile(infile); infile=NULL;

    /* open the file for reading */
    vgmstream->ch[0].streamfile = open_streamfile(filename);

    if (!vgmstream->ch[0].streamfile) goto fail;

    vgmstream->ch[0].channel_start_offset=
        vgmstream->ch[0].offset=start_offset;

    return vgmstream;

fail:
    /* clean up anything we may have opened */
    if (infile) close_streamfile(infile);
    if (vgmstream) close_vgmstream(vgmstream);
    return NULL;
}

/* Some very simple stereo variants of standard dsp just use the standard header
 * twice and add interleave, or just concatenate the channels. We'll support
 * them all here.
 * Note that Cstr isn't here, despite using the form of the standard header,
 * because its loop values are wacky. */

/* .stm
 * Used in Paper Mario 2, Fire Emblem: Path of Radiance, Cubivore
 * I suspected that this was an Intelligent Systems format, but its use in
 * Cubivore calls that into question. */
VGMSTREAM * init_vgmstream_ngc_dsp_stm(const char * const filename) {
    VGMSTREAM * vgmstream = NULL;
    STREAMFILE * infile = NULL;

    struct dsp_header ch0_header, ch1_header;
    int i;
    int stm_header_sample_rate;
    int channel_count;
    const off_t start_offset = 0x100;
    off_t first_channel_size;
    off_t second_channel_start;

    /* check extension, case insensitive */
    /* to avoid collision with Scream Tracker 2 Modules, also ending in .stm
     * and supported by default in Winamp, it was policy in the old days to
     * rename these files to .dsp */
    if (strcasecmp("stm",filename_extension(filename)) &&
            strcasecmp("dsp",filename_extension(filename))) goto fail;

    /* try to open the file for header reading */
    infile = open_streamfile(filename);
    if (!infile) goto fail;

    /* check intro magic */
    if (read_16bitBE(0, infile) != 0x0200) goto fail;

    channel_count = read_32bitBE(4, infile);
    /* only stereo and mono are known */
    if (channel_count != 1 && channel_count != 2) goto fail;

    first_channel_size = read_32bitBE(8, infile);
    /* this is bad rounding, wastes space, but it looks like that's what's
     * used */
    second_channel_start = ((start_offset+first_channel_size)+0x20)/0x20*0x20;

    /* an additional check */
    stm_header_sample_rate = (uint16_t)read_16bitBE(2, infile);

    /* read the DSP headers */
    if (read_dsp_header(&ch0_header, 0x40, infile)) goto fail;
    if (channel_count == 2) {
        if (read_dsp_header(&ch1_header, 0xa0, infile)) goto fail;
    }

    /* checks for fist channel */
    {
        if (ch0_header.sample_rate != stm_header_sample_rate) goto fail;

        /* check initial predictor/scale */
        if (ch0_header.initial_ps != (uint8_t)read_8bit(start_offset, infile))
            goto fail;

        /* check type==0 and gain==0 */
        if (ch0_header.format || ch0_header.gain)
            goto fail;

        if (ch0_header.loop_flag) {
            off_t loop_off;
            /* check loop predictor/scale */
            loop_off = ch0_header.loop_start_offset/16*8;
            if (ch0_header.loop_ps != (uint8_t)read_8bit(start_offset+loop_off,infile))
                goto fail;
        }
    }


    /* checks for second channel */
    if (channel_count == 2) {
        if (ch1_header.sample_rate != stm_header_sample_rate) goto fail;

        /* check for agreement with first channel header */
        if (
            ch0_header.sample_count != ch1_header.sample_count ||
            ch0_header.nibble_count != ch1_header.nibble_count ||
            ch0_header.loop_flag != ch1_header.loop_flag ||
            ch0_header.loop_start_offset != ch1_header.loop_start_offset ||
            ch0_header.loop_end_offset != ch1_header.loop_end_offset
           ) goto fail;

        /* check initial predictor/scale */
        if (ch1_header.initial_ps != (uint8_t)read_8bit(second_channel_start, infile))
            goto fail;

        /* check type==0 and gain==0 */
        if (ch1_header.format || ch1_header.gain)
            goto fail;

        if (ch1_header.loop_flag) {
            off_t loop_off;
            /* check loop predictor/scale */
            loop_off = ch1_header.loop_start_offset/16*8;
            /*printf("loop_start_offset=%x\nloop_ps=%x\nloop_off=%x\n",ch1_header.loop_start_offset,ch1_header.loop_ps,second_channel_start+loop_off);*/
            if (ch1_header.loop_ps != (uint8_t)read_8bit(second_channel_start+loop_off,infile))
                goto fail;
        }
    }

    /* build the VGMSTREAM */

    vgmstream = allocate_vgmstream(channel_count, ch0_header.loop_flag);
    if (!vgmstream) goto fail;

    /* fill in the vital statistics */
    vgmstream->num_samples = ch0_header.sample_count;
    vgmstream->sample_rate = ch0_header.sample_rate;

    vgmstream->loop_start_sample = dsp_nibbles_to_samples(
            ch0_header.loop_start_offset);
    vgmstream->loop_end_sample =  dsp_nibbles_to_samples(
            ch0_header.loop_end_offset)+1;

    /* don't know why, but it does happen*/
    if (vgmstream->loop_end_sample > vgmstream->num_samples)
        vgmstream->loop_end_sample = vgmstream->num_samples;

    vgmstream->coding_type = coding_NGC_DSP;
    vgmstream->layout_type = layout_none;
    vgmstream->meta_type = meta_DSP_STM;

    /* coeffs */
    for (i=0;i<16;i++)
        vgmstream->ch[0].adpcm_coef[i] = ch0_header.coef[i];

    /* initial history */
    /* always 0 that I've ever seen, but for completeness... */
    vgmstream->ch[0].adpcm_history1_16 = ch0_header.initial_hist1;
    vgmstream->ch[0].adpcm_history2_16 = ch0_header.initial_hist2;

    if (channel_count == 2) {
        /* coeffs */
        for (i=0;i<16;i++)
            vgmstream->ch[1].adpcm_coef[i] = ch1_header.coef[i];

        /* initial history */
        /* always 0 that I've ever seen, but for completeness... */
        vgmstream->ch[1].adpcm_history1_16 = ch1_header.initial_hist1;
        vgmstream->ch[1].adpcm_history2_16 = ch1_header.initial_hist2;
    }

    close_streamfile(infile); infile=NULL;

    /* open the file for reading */
    vgmstream->ch[0].streamfile = open_streamfile(filename);

    if (!vgmstream->ch[0].streamfile) goto fail;

    vgmstream->ch[0].channel_start_offset=
        vgmstream->ch[0].offset=start_offset;

    if (channel_count == 2) {
        vgmstream->ch[1].streamfile = open_streamfile(filename);

        if (!vgmstream->ch[1].streamfile) goto fail;

        vgmstream->ch[1].channel_start_offset=
            vgmstream->ch[1].offset=second_channel_start;
    }

    return vgmstream;

fail:
    /* clean up anything we may have opened */
    if (infile) close_streamfile(infile);
    if (vgmstream) close_vgmstream(vgmstream);
    return NULL;
}

/* mpdsp: looks like a standard .dsp header, but the data is actually
 * interleaved stereo 
 * The files originally had a .dsp extension, we rename them to .mpdsp so we
 * can catch this.
 */

VGMSTREAM * init_vgmstream_ngc_mpdsp(const char * const filename) {
    VGMSTREAM * vgmstream = NULL;
    STREAMFILE * infile = NULL;

    struct dsp_header header;
    const off_t start_offset = 0x60;
    int i;

    /* check extension, case insensitive */
    if (strcasecmp("mpdsp",filename_extension(filename))) goto fail;

    /* try to open the file for header reading */
    infile = open_streamfile(filename);
    if (!infile) goto fail;

    if (read_dsp_header(&header, 0, infile)) goto fail;

    /* none have loop flag set, save us from loop code that involves them */
    if (header.loop_flag) goto fail;

    /* check initial predictor/scale */
    if (header.initial_ps != (uint8_t)read_8bit(start_offset,infile))
        goto fail;

    /* check type==0 and gain==0 */
    if (header.format || header.gain)
        goto fail;
        
    /* build the VGMSTREAM */


    /* no loop flag, but they do loop */
    vgmstream = allocate_vgmstream(2,0);
    if (!vgmstream) goto fail;

    /* fill in the vital statistics */
    vgmstream->num_samples = header.sample_count/2;
    vgmstream->sample_rate = header.sample_rate;

    vgmstream->coding_type = coding_NGC_DSP;
    vgmstream->layout_type = layout_interleave;
    vgmstream->interleave_block_size = 0xf000;
    vgmstream->meta_type = meta_DSP_MPDSP;

    /* coeffs */
    for (i=0;i<16;i++) {
        vgmstream->ch[0].adpcm_coef[i] = header.coef[i];
        vgmstream->ch[1].adpcm_coef[i] = header.coef[i];
    }
    
    /* initial history */
    /* always 0 that I've ever seen, but for completeness... */
    vgmstream->ch[0].adpcm_history1_16 = header.initial_hist1;
    vgmstream->ch[0].adpcm_history2_16 = header.initial_hist2;
    vgmstream->ch[1].adpcm_history1_16 = header.initial_hist1;
    vgmstream->ch[1].adpcm_history2_16 = header.initial_hist2;

    close_streamfile(infile); infile=NULL;

    /* open the file for reading */
    for (i=0;i<2;i++) {
        vgmstream->ch[i].streamfile = open_streamfile_buffer(filename,
                vgmstream->interleave_block_size);

        if (!vgmstream->ch[i].streamfile) goto fail;

        vgmstream->ch[i].channel_start_offset=
            vgmstream->ch[i].offset=start_offset+
            vgmstream->interleave_block_size*i;
    }

    return vgmstream;

fail:
    /* clean up anything we may have opened */
    if (infile) close_streamfile(infile);
    if (vgmstream) close_vgmstream(vgmstream);
    return NULL;
}

/* a bunch of formats that are identical except for file extension,
 * but have different interleaves */

VGMSTREAM * init_vgmstream_ngc_dsp_std_int(const char * const filename) {
    VGMSTREAM * vgmstream = NULL;
    STREAMFILE * infile = NULL;

    const off_t start_offset = 0xc0;
    off_t interleave;
    int meta_type;

    struct dsp_header ch0_header,ch1_header;
    int i;

    /* check extension, case insensitive */
    if (strlen(filename) > 7 && !strcasecmp("_lr.dsp",filename+strlen(filename)-7)) {
        /* Bomberman Jetters */
        interleave = 0x14180;
        meta_type = meta_DSP_JETTERS;
    } else if (!strcasecmp("mss",filename_extension(filename))) {
        interleave = 0x1000;
        meta_type = meta_DSP_MSS;
    } else if (!strcasecmp("gcm",filename_extension(filename))) {
        interleave = 0x8000;
        meta_type = meta_DSP_GCM;
    } else goto fail;


    /* try to open the file for header reading */
    infile = open_streamfile(filename);
    if (!infile) goto fail;

    if (read_dsp_header(&ch0_header, 0, infile)) goto fail;
    if (read_dsp_header(&ch1_header, 0x60, infile)) goto fail;

    /* check initial predictor/scale */
    if (ch0_header.initial_ps != (uint8_t)read_8bit(start_offset,infile))
        goto fail;
    if (ch1_header.initial_ps != (uint8_t)read_8bit(start_offset+interleave,infile))
        goto fail;

    /* check type==0 and gain==0 */
    if (ch0_header.format || ch0_header.gain ||
        ch1_header.format || ch1_header.gain)
        goto fail;

    /* check for agreement */
    if (
            ch0_header.sample_count != ch1_header.sample_count ||
            ch0_header.nibble_count != ch1_header.nibble_count ||
            ch0_header.sample_rate != ch1_header.sample_rate ||
            ch0_header.loop_flag != ch1_header.loop_flag ||
            ch0_header.loop_start_offset != ch1_header.loop_start_offset ||
            ch0_header.loop_end_offset != ch1_header.loop_end_offset
       ) goto fail;

    if (ch0_header.loop_flag) {
        off_t loop_off;
        /* check loop predictor/scale */
        loop_off = ch0_header.loop_start_offset/16*8;
        loop_off = (loop_off/interleave*interleave*2) + (loop_off%interleave);
        if (ch0_header.loop_ps != (uint8_t)read_8bit(start_offset+loop_off,infile))
            goto fail;
        if (ch1_header.loop_ps != (uint8_t)read_8bit(start_offset+loop_off+interleave,infile))
            goto fail;
    }

    /* build the VGMSTREAM */

    vgmstream = allocate_vgmstream(2,ch0_header.loop_flag);
    if (!vgmstream) goto fail;

    /* fill in the vital statistics */
    vgmstream->num_samples = ch0_header.sample_count;
    vgmstream->sample_rate = ch0_header.sample_rate;

    /* TODO: adjust for interleave? */
    vgmstream->loop_start_sample = dsp_nibbles_to_samples(
            ch0_header.loop_start_offset);
    vgmstream->loop_end_sample =  dsp_nibbles_to_samples(
            ch0_header.loop_end_offset)+1;

    vgmstream->coding_type = coding_NGC_DSP;
    vgmstream->layout_type = layout_interleave;
    vgmstream->interleave_block_size = interleave;
    vgmstream->meta_type = meta_type;

    /* coeffs */
    for (i=0;i<16;i++) {
        vgmstream->ch[0].adpcm_coef[i] = ch0_header.coef[i];
        vgmstream->ch[1].adpcm_coef[i] = ch1_header.coef[i];
    }
    
    /* initial history */
    /* always 0 that I've ever seen, but for completeness... */
    vgmstream->ch[0].adpcm_history1_16 = ch0_header.initial_hist1;
    vgmstream->ch[0].adpcm_history2_16 = ch0_header.initial_hist2;
    vgmstream->ch[1].adpcm_history1_16 = ch1_header.initial_hist1;
    vgmstream->ch[1].adpcm_history2_16 = ch1_header.initial_hist2;

    close_streamfile(infile); infile=NULL;

    /* open the file for reading */
    for (i=0;i<2;i++) {
        vgmstream->ch[i].streamfile = open_streamfile(filename);

        if (!vgmstream->ch[i].streamfile) goto fail;

        vgmstream->ch[i].channel_start_offset=
            vgmstream->ch[i].offset=start_offset+i*interleave;
    }

    return vgmstream;

fail:
    /* clean up anything we may have opened */
    if (infile) close_streamfile(infile);
    if (vgmstream) close_vgmstream(vgmstream);
    return NULL;
}

