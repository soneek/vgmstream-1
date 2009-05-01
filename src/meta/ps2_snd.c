#include "meta.h"
#include "../util.h"

/* SND (Warriors of Might and Magic Heroes of M&M:Dragonbone Staff) */
VGMSTREAM * init_vgmstream_ps2_snd(STREAMFILE *streamFile) {

	VGMSTREAM * vgmstream = NULL;
	char filename[260];
	off_t start_offset;

    int loop_flag;
    int channel_count;

	/* check extension, case insensitive */
	streamFile->get_name(streamFile,filename,sizeof(filename));
	if (strcasecmp("snd",filename_extension(filename))) goto fail;

	/* check header */
    if (read_32bitBE(0x0,streamFile) !=0x53534e44) goto fail;

    loop_flag = 0;
    channel_count = read_16bitLE(0x0a,streamFile);
	 
    /* build the VGMSTREAM */
    vgmstream = allocate_vgmstream(channel_count,loop_flag);
    if (!vgmstream) goto fail;

    /* fill in the vital statistics */
    start_offset = read_32bitLE(0x04,streamFile)+8;
    vgmstream->sample_rate = (uint16_t)read_16bitLE(0xe,streamFile);
    vgmstream->coding_type = coding_PCM16LE;
    vgmstream->num_samples = (get_streamfile_size(streamFile)-start_offset)/2/channel_count;

    vgmstream->layout_type = layout_interleave;
    vgmstream->interleave_block_size = (uint16_t)read_16bitLE(0x12,streamFile);
    vgmstream->meta_type = meta_PS2_SND;

	/* open the file for reading */
	{
		int i;
		STREAMFILE * file;
		file = streamFile->open(streamFile,filename,STREAMFILE_DEFAULT_BUFFER_SIZE);
		if (!file) goto fail;
		for (i=0;i<channel_count;i++) {
			vgmstream->ch[i].streamfile = file;
			vgmstream->ch[i].channel_start_offset=
              vgmstream->ch[i].offset=start_offset+
              vgmstream->interleave_block_size*i;

		}
	}

	return vgmstream;

  /* clean up anything we may have opened */
fail:
  if (vgmstream) close_vgmstream(vgmstream);
  return NULL;
}
