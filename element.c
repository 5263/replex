/*
 * element.c: MPEG ELEMENTARY STREAM functions for replex
 *        
 *
 * Copyright (C) 2003 - 2006
 *                    Marcus Metzler <mocm@metzlerbros.de>
 *                    Metzler Brothers Systementwicklung GbR
 *           (C) 2006 Reel Multimedia
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "element.h"
#include "mpg_common.h"
#include "pes.h"

unsigned int slots [4] = {12, 144, 0, 0};
const uint16_t bitrates[2][3][15] = {
    { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
      {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 },
      {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 } },
    { {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}
    }
};

const uint16_t freqs[3] = { 44100, 48000, 32000 };

static uint32_t samples[4] = { 384, 1152, 1152, 1536};

char *frames[3] = {"I-Frame","P-Frame","B-Frame"};

unsigned int ac3_bitrates[32] =
    {32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640,
     0,0,0,0,0,0,0,0,0,0,0,0,0};
static uint8_t ac3half[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3};
uint32_t ac3_freq[4] = {480, 441, 320, 0};

#define DEBUG 1

uint64_t add_pts_audio(uint64_t pts, audio_frame_t *aframe, uint64_t frames)
{
	int64_t newpts=0;

	newpts = (pts + (frames *samples [3-aframe->layer] * 27000000ULL) 
		  / aframe->frequency);
	return newpts>0 ? newpts%MAX_PTS2: MAX_PTS2+newpts;
}

int  cfix_audio_count(audio_frame_t *aframe, uint64_t origpts, uint64_t pts)
{
	int64_t diff;
	uint64_t di;
	int c=0;
	di = (samples [3-aframe->layer] * 27000000ULL);
	diff = ptsdiff(origpts,pts);
	c=(aframe->frequency * diff+di/2)/di;
	return c;
}


void fix_audio_count(uint64_t *acount, audio_frame_t *aframe, uint64_t origpts, uint64_t pts)
{
	int c=0;

	c = cfix_audio_count(aframe, origpts, pts);
	*acount += c;
}


uint64_t next_ptsdts_video(uint64_t *pts, sequence_t *s, uint64_t fcount, uint64_t gcount)
{
	int64_t newdts = 0, newpts = 0;
	int64_t fnum = s->current_tmpref - gcount + fcount;

	
	if ( s->pulldown == NOPULLDOWN ) {
		if (s->progressive == TWO_FIELD){
			newdts = ( (fcount-2) * SEC_PER*2 + *pts);
			newpts = ((fnum-1 ) * SEC_PER*2 + *pts);
		} else {
			newdts = ( (fcount-1) * SEC_PER + *pts);
			newpts = ((fnum ) * SEC_PER + *pts);
		} 
	} else {
		uint64_t extra_time = 0;
//		fprintf(stderr,"pulldown %d %d\n",(int) fcount-1, fnum-1);

		if ( s->pulldown == PULLDOWN32)
			extra_time = SEC_PER;
		else 
			extra_time = 3*SEC_PER/2;

		newdts = (fcount - 1) * 5ULL * SEC_PER / 4ULL + 
			((fcount - 1)%2)*extra_time + 
			*pts;

		if ((s->pulldown == PULLDOWN23) && (fcount-1))
			newdts -= SEC_PER/2;

		newpts = SEC_PER +
			(fnum -1) * 5ULL * SEC_PER / 4ULL + 
			((fnum - 1)%2)*extra_time + 
			*pts;
		
	}


	*pts = newpts >= 0 ? newpts%MAX_PTS2: MAX_PTS2+newpts;
	return newdts >= 0 ? newdts%MAX_PTS2: MAX_PTS2+newdts;
}

void fix_video_count(sequence_t *s, uint64_t *frame, uint64_t origpts, uint64_t pts,
		     uint64_t origdts, uint64_t dts)
{
	int64_t pdiff = 0;
	int64_t ddiff = 0;
	int64_t pframe = 0;
	int64_t dframe = 0;
	int psig=0;
	int dsig=0;
	int64_t fr=0;

	pdiff = ptsdiff(origpts,pts);
	ddiff = ptsdiff(origdts,dts);
	psig = pdiff > 0;
	dsig = ddiff > 0;
	if (!psig) pdiff = -pdiff;
	if (!dsig) ddiff = -ddiff;

	if ( s->pulldown == NOPULLDOWN ) {
		dframe = (ddiff+SEC_PER/2ULL) / SEC_PER;
		pframe = (pdiff+SEC_PER/2ULL) / SEC_PER;
	} else {
		dframe = (4ULL*ddiff/5ULL+SEC_PER/2ULL) / SEC_PER;
		pframe = (4ULL*pdiff/5ULL+SEC_PER/2ULL) / SEC_PER;
	}

	if (!psig) fr = -(int)pframe;
	else fr = (int)pframe;
	if (!dsig) fr -= (int)dframe;
	else fr += (int)dframe;
	*frame = *frame + fr/2;
	if (fr/2) fprintf(stderr,"fixed video frame %d\n",
			  (int)fr/2);
}

int FindPacketHeader(const uint8_t *Data, int s, int l);

void inserttime(uint8_t *buf, uint8_t h, uint8_t m, uint8_t s)
{
	buf[4] &= ~(0x7F); 
	buf[4] |= (h & 0x1F) << 2;
	buf[4] |= (m & 0x30) >> 4;

	buf[5] &= ~(0xF7);
	buf[5] |= (m & 0x0F) << 4;
	buf[5] |= (s & 0x38) >> 3;

	buf[6] &= ~(0xE0);
        buf[6] |= (s & 0x07) << 5;
}

void pts2time(uint64_t pts, uint8_t *buf, int len)
{
	uint8_t h,m,s;
	int c=0,x;
//	uint8_t *data=buf;

	pts = (pts/300)%MAX_PTS;
	h = (uint8_t)(pts/90000)/3600;
	m = (uint8_t)((pts/90000)%3600)/60;
	s = (uint8_t)((pts/90000)%3600)%60;

	while(c+7 < len) {
		x=FindPacketHeader(buf, c, len);
		if (x==-1)
			return;
		if (buf[x+3]==GROUP_START_CODE && (buf[x+5] & 0x08)) {
			inserttime(buf+x, h, m, s);
		}
		c=x+4;
	}
		
}	
void pts2timex(uint64_t pts, uint8_t *buf, int len)
{
	uint8_t h,m,s;
	int c = 0;

	pts = (pts/300)%MAX_PTS;
	h = (uint8_t)(pts/90000)/3600;
	m = (uint8_t)((pts/90000)%3600)/60;
	s = (uint8_t)((pts/90000)%3600)%60;

	while (c+7 < len){
		if (buf[c] == 0x00 && buf[c+1] == 0x00 && buf[c+2] == 0x01 && 
		    buf[c+3] == GROUP_START_CODE && (buf[c+5] & 0x08)){
			inserttime(buf+c,h,m,s);
/* 1hhhhhmm|mmmm1sss|sss */
/*
			c+=4;
			fprintf(stderr,"fixed time\n");
			fprintf(stderr,"%02d:", (int)((buf[c]>>2)& 0x1F));
                        fprintf(stderr,"%02d:", (int)(((buf[c]<<4)& 0x30)|((buf[c+1]>>4)& 0x0F)));
                        fprintf(stderr,"%02d\n",(int)(((buf[c+1]<<3)& 0x38)|((buf[c+2]>>5)& 0x07)));
*/
			c = len;


		} else c++;
	}

}


int get_video_info(ringbuffer *rbuf, sequence_t *s, long off, int le)
{
        uint8_t buf[150];
        uint8_t *headr;
        int sw,i;
        int form = -1;
        int c = 0;
	int re = 0;


        s->set = 0;
        s->ext_set = 0;
	s->pulldown_set = 0;
        if ((re = ring_find_mpg_header(rbuf, SEQUENCE_HDR_CODE, off, le)) < 0)
		return re;
	headr = buf+4;
	if (ring_peek(rbuf, buf, 150, off) < 0) return -2;
	
	s->h_size	= ((headr[1] &0xF0) >> 4) | (headr[0] << 4);
	s->v_size	= ((headr[1] &0x0F) << 8) | (headr[2]);
    
        sw = (int)((headr[3]&0xF0) >> 4) ;

	if (DEBUG){
		switch( sw ){
		case 1:
			fprintf(stderr,"Video: aspect ratio: 1:1");
			s->aspect_ratio = 100;        
			break;
		case 2:
			fprintf(stderr,"Video: aspect ratio: 4:3");
			s->aspect_ratio = 133;        
			break;
		case 3:
			fprintf(stderr,"Video: aspect ratio: 16:9");
			s->aspect_ratio = 177;        
			break;
		case 4:
			fprintf(stderr,"Video: aspect ratio: 2.21:1");
			s->aspect_ratio = 221;        
			break;
			
		case 5 ... 15:
			fprintf(stderr,"Video: aspect ratio: reserved");
			s->aspect_ratio = 0;        
			break;
			
		default:
			s->aspect_ratio = 0;        
			return -3;
		}
	}

        if (DEBUG) fprintf(stderr,"  size = %dx%d",s->h_size,s->v_size);

        sw = (int)(headr[3]&0x0F);

        switch ( sw ) {
	case 1:
                s->frame_rate = 23976;
		form = -1;
		break;
	case 2:
                s->frame_rate = 24000;
		form = -1;
		break;
	case 3:
                s->frame_rate = 25000;
		form = VIDEO_PAL;
		break;
	case 4:
                s->frame_rate = 29970;
		form = VIDEO_NTSC;
		break;
	case 5:
                s->frame_rate = 30000;
		form = VIDEO_NTSC;
		break;
	case 6:
                s->frame_rate = 50000;
		form = VIDEO_PAL;
		break;
	case 7:
                s->frame_rate = 60000;
		form = VIDEO_NTSC;
		break;
	}
	if (DEBUG) fprintf(stderr,"  frame rate: %2.3f fps",s->frame_rate/1000.);

	s->bit_rate = (((headr[4] << 10) & 0x0003FC00UL) 
		       | ((headr[5] << 2) & 0x000003FCUL) | 
		       (((headr[6] & 0xC0) >> 6) & 0x00000003UL));
	
        if (DEBUG) fprintf(stderr,"  bit rate: %.2f Mbit/s",400*(s->bit_rate)/1000000.);
        if (DEBUG) fprintf(stderr,"\n");

        s->video_format = form;

                

	s->vbv_buffer_size = (( headr[7] & 0xF8) >> 3 ) | (( headr[6] & 0x1F )<< 5);	
	s->flags	   = ( headr[7] & 0x06);	
	if (DEBUG) fprintf(stderr, "  vbvbuffer %d\n",16*1024*(s->vbv_buffer_size));

	c += 8;
	if ( !(s->flags & INTRAQ_FLAG) ) 
		s->flags = ( headr[7] & 0x07);
	else {
		s->flags |= headr[c+63] & 0x01;
		memset(s->intra_quant, 0, 64);
		for (i=0;i<64;i++)
			s->intra_quant[i] |= (headr[c+i] >> 1) |
				(( headr[c-1+i] & 0x01) << 7);

		c += 64;
	}
	if (s->flags & NONINTRAQ_FLAG){
		memcpy(s->non_intra_quant, headr+c, 64);
		c += 64;
	}
	s->set=1;

	return c;
}

int find_audio_sync(ringbuffer *rbuf, uint8_t *buf, long off, int type, int le)
{
	int found = 0;
	int c=0;
	int l;
	uint8_t b1,b2,m2;
	int r=0;

	memset(buf,0,7);
	b1 = 0x00;
	b2 = 0x00;
	m2 = 0xFF;
	switch(type){
	case AC3:
		b1 = 0x0B;
		b2 = 0x77;
		l = 6;
		break;

	case MPEG_AUDIO:
		b1 = 0xFF;
		b2 = 0xF8;
		m2 = 0xF8;
		l = 4;
		break;

	default:
		return -1;
	}

	c = off;
	while ( c-off < le){
		uint8_t b;
		if ((r = mring_peek(rbuf, &b, 1, c)) <0) return -1;
		switch(found){

		case 0:
			if ( b == b1) found = 1;
			break;
			
		case 1:
			if ( (b&m2) == b2){
				if ((r = mring_peek(rbuf, buf, l, c-1)) < -1) 
					return -2;
				return c-1-off;	
			} else if ( b != b1) found = 0;
		}
		c++;
	}	
	if (found) return -2;
	return -1;
}

int find_audio_s(uint8_t *rbuf, long off, int type, int le)
{
	int found = 0;
	int c=0;
	int l;
	uint8_t b1,b2,m2;

	b1 = 0x00;
	b2 = 0x00;
	m2 = 0xFF;
	switch(type){
	case AC3:
		b1 = 0x0B;
		b2 = 0x77;
		l = 6;
		break;

	case MPEG_AUDIO:
		b1 = 0xFF;
		b2 = 0xF8;
		m2 = 0xF8;
		l = 4;
		break;

	default:
		return -1;
	}

	c = off;
	while ( c < le){
		uint8_t b=rbuf[c];
		switch(found){
		case 0:
			if ( b == b1) found = 1;
			break;
			
		case 1:
			if ( (b&m2) == b2){
				return c-1;	
			} else if ( b != b1) found = 0;
		}
		c++;
	}	
	if (found) return -2;
	return -1;
}

static int calculate_mpg_framesize(audio_frame_t *af)
{
        int frame_size;

        frame_size = af->bit_rate/1000;
        if (!frame_size) return -1;
        switch(af->layer) {
        case 1:
                frame_size = (frame_size * 12000) / af->sample_rate;
                frame_size = (frame_size + af->padding) * 4;
                break;
        case 2:
                frame_size = (frame_size * 144000) / af->sample_rate;
                frame_size += af->padding;
                break;
        default:
        case 3:
                frame_size = (frame_size * 144000) / (af->sample_rate << af->lsf);
                frame_size += af->padding;
                break;
        }
        return frame_size;
}

int check_audio_header(ringbuffer *rbuf, audio_frame_t * af, long  off, int le, 
		       int type)
{
	uint8_t headr[7];
	uint8_t frame;
	int fr;
	int half = 0;
	int c=0;
	
	if ( (c = find_audio_sync(rbuf, headr, off, type, le))
	     != 0 ) {
		if (c==-2){
			fprintf(stderr,"Incomplete audio header\n");
			return -2;
		}
		fprintf(stderr,"Error in audio header\n");
		return -1;
	}
	switch (type){

	case MPEG_AUDIO:
		if ( af->layer != 4 -((headr[1] & 0x06) >> 1) ){
			if ( headr[1] == 0xff){
				return -3;
			} else {
#ifdef IN_DEBUG
				fprintf(stderr,"Wrong audio layer\n");
#endif
				return -1;
			}
		}
                if ( af->bit_rate !=
                     (  af->bit_rate = bitrates[af->lsf][af->layer-1][(headr[2] >> 4 )]*1000)){
#ifdef IN_DEBUG
                        fprintf(stderr,"Wrong audio bit rate\n");
#endif
                        return -1;
                }

                if (af->padding != ((headr[2] >> 1) & 1)){
			int fsize;
			af->padding = (headr[2] >> 1) & 1;
                        if ((fsize = calculate_mpg_framesize(af)) < 0 || 
			     abs(fsize - af->framesize) >2) return -1;
			af->framesize = fsize;
#ifdef IN_DEBUG
                        fprintf(stderr,"padding changed : %d\n",af->padding);
#endif

                }

		break;
		
	case AC3:
		frame = (headr[4]&0x3F);
		if (af->bit_rate != ac3_bitrates[frame>>1]*1000){
#ifdef IN_DEBUG
			fprintf(stderr,"Wrong audio bit rate\n");
#endif
			return -1;
		}
		half = ac3half[headr[5] >> 3];
		fr = (headr[4] & 0xc0) >> 6;
		if (af->frequency != ((ac3_freq[fr] *100) >> half)){
#ifdef IN_DEBUG
			fprintf(stderr,"Wrong audio frequency\n");
#endif
			return -1;
		}
		
		break;

	}

	return 0;
}


int get_audio_info(ringbuffer *rbuf, audio_frame_t *af, long off, int le, int verb) 
{
	int c = 0;
	int fr =0;
	uint8_t headr[4];
        int sample_rate_index;

	af->set=0;

	if ( (c = find_audio_sync(rbuf, headr, off, MPEG_AUDIO,le)) < 0 ) 
		return c;

        af->layer = 4 - ((headr[1] & 0x06) >> 1);
//        if (af->layer >3) return -1;

        if (DEBUG && verb)
		fprintf(stderr,"Audiostream: layer: %d", af->layer);
        if (headr[1] & (1<<4)) {
                af->lsf = (headr[1] & (1<<3)) ? 0 : 1;
                af->mpg25 = 0;
		if (DEBUG && verb)
			fprintf(stderr,"  version: 1");
        } else {
                af->lsf = 1;
                af->mpg25 = 1;
		if (DEBUG && verb)
			fprintf(stderr,"  version: 2");
        }
        /* extract frequency */
        sample_rate_index = (headr[2] >> 2) & 3;
        if (sample_rate_index > 2) return -1;
        af->sample_rate = freqs[sample_rate_index] >> (af->lsf + af->mpg25);

	af->padding = (headr[2] >> 1) & 1;


        af->bit_rate = bitrates[af->lsf][af->layer-1][(headr[2] >> 4 )]*1000;


	if (DEBUG && verb){
		if (af->bit_rate == 0)
			fprintf (stderr,"  Bit rate: free");
		else if (af->bit_rate == 0xf)
			fprintf (stderr,"  BRate: reserved");
		else
			fprintf (stderr,"  BRate: %d kb/s", af->bit_rate/1000);
	}

	fr = (headr[2] & 0x0c ) >> 2;
	af->frequency = freqs[fr];


	if (DEBUG && verb){
		if (af->frequency == 3)
			fprintf (stderr, "  Freq: reserved");
		else
			fprintf (stderr,"  Freq: %2.1f kHz", 
				 af->frequency/1000.);
	}
	af->off = c;
	af->set = 1;
	af->framesize = calculate_mpg_framesize(af);
	//af->framesize = af->bit_rate *slots [3-af->layer]/ af->frequency;
	if (DEBUG && verb) fprintf(stderr," frame size: %d \n", af->framesize);
	return c;
}

int get_ac3_info(ringbuffer *rbuf, audio_frame_t *af, long off, int le, int verb)
{
	int c=0;
	uint8_t headr[6];
	uint8_t frame;
	int half = 0;
	int fr;
	

	af->set=0;

	if ((c = find_audio_sync(rbuf, headr, off, AC3, le)) < 0 ) 
		return c;

	af->off = c;

	af->layer = 0;  // 0 for AC3

	if (DEBUG && verb) fprintf (stderr,"AC3 stream:");
	frame = (headr[4]&0x3F);
	af->bit_rate = ac3_bitrates[frame>>1]*1000;
	half = ac3half[headr[5] >> 3];
	if (DEBUG && verb) fprintf (stderr,"  bit rate: %d kb/s", af->bit_rate/1000);
	fr = (headr[4] & 0xc0) >> 6;
	af->frequency = (ac3_freq[fr] *100) >> half;
	
	if (DEBUG && verb) fprintf (stderr,"  freq: %d Hz\n", af->frequency);

	switch (headr[4] & 0xc0) {
	case 0:
		af->framesize = 4 * af->bit_rate/1000;
		break;

	case 0x40:
		af->framesize = 2 * (320 * af->bit_rate / 147000 + (frame & 1));
		break;

	case 0x80:
		af->framesize = 6 * af->bit_rate/1000;
		break;
	}

	if (DEBUG && verb) fprintf (stderr,"  frame size %d\n", af->framesize);

	af->off = c;
	af->set = 1;
	return c;
}


int get_video_ext_info(ringbuffer *rbuf, sequence_t *s, long off, int le)
{
        uint8_t *headr;
        uint8_t buf[12];
	uint8_t ext_id;
	int re=0;

        if (( re =ring_find_mpg_header(rbuf, EXTENSION_START_CODE, off, le)) < 0){
		fprintf(stderr,"Error in find_mpg_header");
		return re;
	}

	if (ring_peek(rbuf, buf, 5, off) < 0) return -2;
	headr=buf+4;
	
	ext_id = (headr[0]&0xF0) >> 4;

	switch (ext_id){
	case SEQUENCE_EXTENSION:{
		uint16_t hsize;
		uint16_t vsize;
		uint32_t vbvb;
		uint32_t bitrate;
		uint8_t  fr_n, fr_d;


		if (s->ext_set || !s->set) break;
		if (ring_peek(rbuf, buf, 10, off) < 0) return -2;
		headr=buf+4;

		if (DEBUG) fprintf(stderr,"Sequence Extension:");
		s->profile = ((headr[0]&0x0F) << 4) | ((headr[1]&0xF0) >> 4);
		if (headr[1]&0x08){
			s->progressive = 1;
			if (DEBUG) fprintf(stderr," progressive sequence ");
		} else s->progressive = 0;
		s->chroma = (headr[1]&0x06)>>1;
		if (DEBUG){
			switch(s->chroma){
			case 0:
				fprintf(stderr," chroma reserved ");
				break;
			case 1:
				fprintf(stderr," chroma 4:2:0 ");
				break;
			case 2:
				fprintf(stderr," chroma 4:2:2 ");
				break;
			case 3:
				fprintf(stderr," chroma 4:4:4 ");
				break;
			}
		}
		
		hsize = ((headr[1]&0x01)<<12) | ((headr[2]&0x80)<<6);
		vsize = ((headr[2]&0x60)<<7);
		s->h_size	|= hsize;
		s->v_size	|= vsize;
		if (DEBUG) fprintf(stderr,"  size = %dx%d",s->h_size,s->v_size);
		
		bitrate = ((headr[2]& 0x1F) << 25) | (( headr[3] & 0xFE ) << 17);
		s->bit_rate |= bitrate;
	
		if (DEBUG) fprintf(stderr,"  bit rate: %.2f Mbit/s",400.*(s->bit_rate)/1000000.);


		vbvb = (headr[4]<<10);
		s->vbv_buffer_size |= vbvb;
		if (DEBUG) fprintf(stderr, "  vbvbuffer %d",16*1024*(s->vbv_buffer_size));
		fr_n = (headr[5] & 0x60) >> 6;
		fr_d = (headr[5] & 0x1F);
	
		s->frame_rate = s->frame_rate * (fr_n+1) / (fr_d+1);
		if (DEBUG) fprintf(stderr,"  frame rate: %2.3f\n", s->frame_rate/1000.);
		s->ext_set=1;
		break;
	}

	case SEQUENCE_DISPLAY_EXTENSION:
		break;

	case PICTURE_CODING_EXTENSION:{
		int repeat_first = 0;
		int top_field = 0;
		int prog_frame = 0;

		if (!s->set || s->pulldown_set) break;
		if (ring_peek(rbuf, buf, 10, off) < 0) return -2;
		headr=buf+4;
		
		if ( (headr[2]&0x03) != 0x03 ) break; // not frame picture
		if ( (headr[3]&0x02) ) repeat_first = 1; // repeat flag set => pulldown
		if ( (headr[3]&0x80) ) top_field=1; 
		if ( (headr[4]&0x80) ) prog_frame=1; 
 
		if (repeat_first) {
			if (!s->progressive){
				if (s->current_tmpref)
					s->pulldown = PULLDOWN23;
				else
					s->pulldown = PULLDOWN32;
				s->pulldown_set = 1;
			} else {
				if(prog_frame && top_field)
					s->progressive = TWO_FIELD;
//				if (DEBUG) fprintf(stderr,"Picture Coding Extension: progressive tmp %d top %d prog %d rep %d\n", s->current_tmpref, top_field, prog_frame, repeat_first);
			}
		}

		if (DEBUG){
			switch (s->pulldown) {
			case PULLDOWN32:
				fprintf(stderr,"Picture Coding Extension:");
				fprintf(stderr," 3:2 pulldown detected \n");
				break;
			case PULLDOWN23:
				fprintf(stderr,"Picture Coding Extension:");
				fprintf(stderr," 2:3 pulldown detected \n");
				break;
//			default:
				//fprintf(stderr," no pulldown detected \n");
			}
		}
		break;
	}
	case QUANT_MATRIX_EXTENSION:
		break;
	case PICTURE_DISPLAY_EXTENSION:
		break;
	}
	
	
	return ext_id;
}

