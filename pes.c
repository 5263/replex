/*
 * pes.c: MPEG PES functions for replex
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

#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>

#include "pes.h"

//#define PES_DEBUG

void printpts(int64_t pts)
{
	if (pts < 0){
		fprintf(stderr,"-");
		pts = -pts;
	}
	pts = pts/300;
	pts &= (MAX_PTS-1);
	fprintf(stderr,"%2d:%02d:%02d.%03d ",
		(unsigned int)(pts/90000.)/3600,
		((unsigned int)(pts/90000.)%3600)/60,
		((unsigned int)(pts/90000.)%3600)%60,
		(((unsigned int)(pts/90.)%3600000)%60000)%1000
		);
}

void printptss(int64_t pts)
{
	if (pts < 0){
		fprintf(stdout,"-");
		pts = -pts;
	}
	pts = pts/300;
	pts &= (MAX_PTS-1);
	fprintf(stdout,"%2d:%02d:%02d.%03d ",
		(unsigned int)(pts/90000.)/3600,
		((unsigned int)(pts/90000.)%3600)/60,
		((unsigned int)(pts/90000.)%3600)%60,
		(((unsigned int)(pts/90.)%3600000)%60000)%1000
		);
}

/* use if you know that ptss are close and may overflow */
int64_t ptsdiff(uint64_t pts1, uint64_t pts2)
{
	switch (ptscmp(pts1, pts2)){
	case 0:
		return 0;
		break;

	case 1:
	case -2:
		return (pts1 -pts2);
		break;

	case 2:
		return (pts1 + MAX_PTS2 -pts2);
		break;

	case -1:
		return (pts1 - (pts2+ MAX_PTS2));
		break;

	}

	return 0;
}

/* use, if you need  an unsigned result in pts range */
uint64_t uptsdiff(uint64_t pts1, uint64_t pts2)
{
	int64_t diff;

	diff = pts1 - pts2;
		
	if (diff < 0){
		diff = MAX_PTS2 +diff;
	}
	return diff;
}

int ptscmp(uint64_t pts1, uint64_t pts2)
{
	int ret;

	if (pts1 > pts2){
		if ((pts1 - pts2) > MAX_PTS2/2)
			ret = -1;
		else 
			ret = 1;
	} else if (pts1 == pts2) ret = 0;
	else {
		if ((pts2 - pts1) > MAX_PTS2/2)
			ret = 2;
		else 
			ret = -2;
	}
/*
	fprintf(stderr,"PTSCMP: %lli %lli %d\n", pts1, pts2, ret);
	printpts(pts1);
	printpts(pts2);
	fprintf(stderr,"\n");
*/
	return ret;
}

uint64_t ptsadd(uint64_t pts1, uint64_t pts2)
{
	ptsinc(&pts1,pts2);
	return pts1;

}

void init_pes_in(pes_in_t *p, int t, ringbuffer *rb, int wi){
	p->type = t;
        p->found = 0;
        p->cid = 0;
        p->mpeg = 0;
	p->withbuf = wi;
	
	if (p->withbuf && !p->buf){
		p->buf = malloc(MAX_PLENGTH*sizeof(uint8_t));
		memset(p->buf,0,MAX_PLENGTH*sizeof(uint8_t));
	} else if (rb) p->rbuf = rb;
	if (p->rbuf) p->ini_pos = ring_wpos(p->rbuf); 
        p->done = 0;
	memset(p->pts, 0 , 5);
	memset(p->dts, 0 , 5);
}


void get_pes (pes_in_t *p, uint8_t *buf, int count, void (*func)(pes_in_t *p))
{

	int l=0;
	unsigned short *pl=NULL;
	int c=0;

	uint8_t headr[3] = { 0x00, 0x00, 0x01} ;
	while (c < count && (!p->mpeg ||
			     (p->mpeg == 2 && p->found < 9))
	       &&  (p->found < 5 || !p->done)){
		switch ( p->found ){
		case 0:
		case 1:
			if (buf[c] == 0x00) p->found++;
			else p->found = 0;
			c++;
			break;
		case 2:
			if (buf[c] == 0x01) p->found++;
			else if (buf[c] == 0){
				p->found = 2;
			} else p->found = 0;
			c++;
			break;
		case 3:
			p->cid = 0;
			switch (buf[c]){
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
				p->done = 1;
			case PRIVATE_STREAM1:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
				p->found++;
				p->cid = buf[c];
				c++;
				break;
			default:
			case PACK_START:
			case SYS_START:
				p->found = 0;
				c++;
				break;
			}
			break;
			

		case 4:
			if (count-c > 1){
				pl = (unsigned short *) (buf+c);
				p->plength =  ntohs(*pl);
				p->plen[0] = buf[c];
				c++;
				p->plen[1] = buf[c];
				c++;
				p->found+=2;
			} else {
				p->plen[0] = buf[c];
				p->found++;
				return;
			}
			break;
		case 5:
			p->plen[1] = buf[c];
			c++;
			pl = (unsigned short *) p->plen;
			p->plength = ntohs(*pl);
			p->found++;
			break;


		case 6:
			if (!p->done){
				p->flag1 = buf[c];
				c++;
				p->found++;
				if ( (p->flag1 & 0xC0) == 0x80 ) p->mpeg = 2;
				else {
					fprintf(stderr, "Error in PES Header 0x%2x\n",p->cid);
					p->found = 0;
				}
			}
			break;

		case 7:
			if ( !p->done && p->mpeg == 2){
				p->flag2 = buf[c];
				c++;
				p->found++;
			}	
			break;

		case 8:
			if ( !p->done && p->mpeg == 2){
				p->hlength = buf[c];
				c++;
				p->found++;
			}
			break;
			
		default:

			break;
		}
		if(p->plength && p->found == 9 && p->found > p->plength+6){
			fprintf(stderr, "Error in PES Header 0x%2x\n",p->cid);
			p->found = 0;
		}
	}

	if (!p->plength) p->plength = MMAX_PLENGTH-6;


	if ( p->done || (p->mpeg == 2 && p->found >= 9) ){
		switch (p->cid){
			
		case AUDIO_STREAM_S ... AUDIO_STREAM_E:			
		case VIDEO_STREAM_S ... VIDEO_STREAM_E:
		case PRIVATE_STREAM1:

			if (p->withbuf){
				memcpy(p->buf, headr, 3);
				p->buf[3] = p->cid;
				memcpy(p->buf+4,p->plen,2);
			} else {
				memcpy(p->hbuf, headr, 3);
				p->hbuf[3] = p->cid;
				memcpy(p->hbuf+4,p->plen,2);
			}

			if (p->found == 9){
				if (p->withbuf){
					p->buf[6] = p->flag1;
					p->buf[7] = p->flag2;
					p->buf[8] = p->hlength;
				} else {
					p->hbuf[6] = p->flag1;
					p->hbuf[7] = p->flag2;
					p->hbuf[8] = p->hlength;
				}
			}

			if ( (p->flag2 & PTS_ONLY) &&  p->found < 14){
				while (c < count && p->found < 14){
					p->pts[p->found-9] = buf[c];
					if (p->withbuf)
						p->buf[p->found] = buf[c];
					else 
						p->hbuf[p->found] = buf[c];
					c++;
					p->found++;
				}
				if (c == count) return;
			}

			if (((p->flag2 & PTS_DTS) == 0xC0) && p->found < 19){
				while (c < count && p->found < 19){
					p->dts[p->found-14] = buf[c];
					if (p->withbuf)
						p->buf[p->found] = buf[c];
					else 
						p->hbuf[p->found] = buf[c];
					c++;
					p->found++;
				}
				if (c == count) return;
			}


			while (c < count && p->found < p->plength+6){
				l = count -c;
				if (l+p->found > p->plength+6)
					l = p->plength+6-p->found;
				if (p->withbuf)
					memcpy(p->buf+p->found, buf+c, l);
				else {
					if ( p->found < p->hlength+9 ){
						int rest = p->hlength+9-p->found;
						memcpy(p->hbuf+p->found, buf+c, rest);
						if (ring_write(p->rbuf, buf+c+rest, 
							       l-rest) <0){
							fprintf(stderr,
								"ring buffer overflow in get_pes %d\n"
								,p->rbuf->size);
							exit(1);
						}
					} else {
						if (ring_write(p->rbuf, buf+c, l)<0){
							fprintf(stderr,
								"ring buffer overflow in get_pes %d\n"
								,p->rbuf->size);
							exit(1);
						}
					}
				}

				p->found += l;
				c += l;
			}			
			if(p->found == p->plength+6){
				func(p);
			}
			break;
		}

		if ( p->done ){
			if( p->found + count - c < p->plength+6){
				p->found += count-c;
				c = count;
			} else {
				c += p->plength+6 - p->found;
				p->found = p->plength+6;
			}
		}

		if (p->plength && p->found == p->plength+6) {
			init_pes_in(p, p->type, NULL, p->withbuf);
			if (c < count)
				get_pes(p, buf+c, count-c, func);
		}
	}
	return;
}


uint32_t scr_base_ps(uint8_t *scr) 
{
	uint32_t base = 0;
	uint8_t *buf = (uint8_t *)&base;
	
	buf[0] |= (uint8_t)((scr[0] & 0x18) << 3);
	buf[0] |= (uint8_t)((scr[0] & 0x03) << 4);
	buf[0] |= (uint8_t)((scr[1] & 0xF0) >> 4);
		 
	buf[1] |= (uint8_t)((scr[1] & 0x0F) << 4);
	buf[1] |= (uint8_t)((scr[2] & 0xF0) >> 4);

	buf[2] |= (uint8_t)((scr[2] & 0x08) << 4);
	buf[2] |= (uint8_t)((scr[2] & 0x03) << 5);
	buf[2] |= (uint8_t)((scr[3] & 0xF8) >> 3);

	buf[3] |= (uint8_t)((scr[3] & 0x07) << 5);
	buf[3] |= (uint8_t)((scr[4] & 0xF8) >> 3);

	base = ntohl(base);
	return base;
}

uint16_t scr_ext_ps(uint8_t *scr)
{
	short ext = 0;

	ext = (short)(scr[5] >> 1);
	ext += (short) (scr[4] &  0x03) * 128;

	return ext;
}



void init_ps(ps_packet *p)
{
        p->stuff_length=0xF8;
        p->data = NULL;
        p->sheader_length = 0;
        p->audio_bound = 0;
        p->video_bound = 0;
        p->npes = 0;
}

void kill_ps(ps_packet *p)
{
        if (p->data)
                free(p->data);
        init_ps(p);
}

void setlength_ps(ps_packet *p)
{
        short *ll;
        ll = (short *) p->sheader_llength;
	p->sheader_length = ntohs(*ll) - 6;
}

static void setl_ps(ps_packet *p)
{
        setlength_ps(p);
        p->data = (uint8_t *) malloc(p->sheader_length);
}


int cwrite_ps(uint8_t *buf, ps_packet *p, uint32_t length)
{
        long count,i;
        uint8_t headr1[4] = {0x00, 0x00, 0x01, PACK_START };
        uint8_t headr2[4] = {0x00, 0x00, 0x01, SYS_START };
        uint8_t buffy = 0xFF;


        memcpy(buf,headr1,4);
        count = 4;
	memcpy(buf+count,p->scr,6);
	count += 6;
	memcpy(buf+count,p->mux_rate,3);
	count += 3;
	memcpy(buf+count,&p->stuff_length,1);
	count++;
	for(i=0; i< (p->stuff_length & 3); i++){
		memcpy(buf+count,&buffy,1);
		count++;
	}

        if (p->sheader_length){
                memcpy(buf+count,headr2,4);
                count += 4;
                memcpy(buf+count,p->sheader_llength,2);
                count += 2;
		memcpy(buf+count,p->rate_bound,3);
		count += 3;
		memcpy(buf+count,&p->audio_bound,1);
		count++;
		memcpy(buf+count,&p->video_bound,1);
		count++;
		memcpy(buf+count,&p->reserved,1);
		count++;
                memcpy(buf+count,p->data,p->sheader_length);
                count += p->sheader_length;
        }

        return count;
}



int write_ps_header(uint8_t *buf, 
		    uint64_t   SCR, 
		    uint32_t   muxr,
		    uint8_t    audio_bound,
		    uint8_t    fixed,
		    uint8_t    CSPS,
		    uint8_t    audio_lock,
		    uint8_t    video_lock,
		    uint8_t    video_bound,
		    uint8_t    navpack)
{
	ps_packet p;
	uint8_t *scr;
	uint32_t lscr;
	uint16_t scr_ext = 0;

	init_ps(&p);
	
	lscr = htonl((uint32_t) ((SCR/300ULL) & 0x00000000FFFFFFFF));
	scr = (uint8_t *) &lscr;
	scr_ext = (uint16_t) ((SCR%300ULL) & 0x00000000000001FF);
	
// SCR = 0
	p.scr[0] = 0x44;
	p.scr[1] = 0x00;
	p.scr[2] = 0x04;
	p.scr[3] = 0x00;
	p.scr[4] = 0x04;
	p.scr[5] = 0x01;
	
	p.scr[0] = 0x44 | ((scr[0] >> 3)&0x18) | ((scr[0] >> 4)&0x03);
	p.scr[1] = 0x00 | ((scr[0] << 4)&0xF0) | ((scr[1] >> 4)&0x0F);
	p.scr[2] = 0x04 | ((scr[1] << 4)&0xF0) | ((scr[2] >> 4)&0x08)
		| ((scr[2] >> 5)&0x03);
	p.scr[3] = 0x00 | ((scr[2] << 3)&0xF8) | ((scr[3] >> 5)&0x07);
	p.scr[4] = 0x04 | ((scr[3] << 3)&0xF8) | ((scr_ext >> 7)&0x03);
	p.scr[5] = 0x01 | ((scr_ext << 1)&0xFF);

	
	muxr = muxr/50;
	p.mux_rate[0] = (uint8_t)(muxr >> 14);
	p.mux_rate[1] = (uint8_t)(0xff & (muxr >> 6));
	p.mux_rate[2] = (uint8_t)(0x03 | ((muxr & 0x3f) << 2));

	p.stuff_length = 0xF8;
	
	if (navpack){
		p.sheader_llength[0] = 0x00;
		p.sheader_llength[1] = 0x12;

		setl_ps(&p);
		
		p.rate_bound[0] = (uint8_t)(0x80 | (muxr >>15));
		p.rate_bound[1] = (uint8_t)(0xff & (muxr >> 7));
		p.rate_bound[2] = (uint8_t)(0x01 | ((muxr & 0x7f)<<1));

		p.audio_bound = (uint8_t)((audio_bound << 2)|(fixed << 1)|CSPS);
		p.video_bound = (uint8_t)((audio_lock << 7)|
				     (video_lock << 6)|0x20|video_bound);
		p.reserved = (uint8_t)(0xFF >> 1);

		p.data[0] = 0xB9;  
		p.data[1] = 0xE0;  
		p.data[2] = 0xE8;  
		p.data[3] = 0xB8;  
		p.data[4] = 0xC0;  
		p.data[5] = 0x20;  
		p.data[6] = 0xbd;  
		p.data[7] = 0xe0;  
		p.data[8] = 0x3a;  
		p.data[9] = 0xBF;  
		p.data[10] = 0xE0;  
		p.data[11] = 0x02;  

		cwrite_ps(buf, &p, PS_HEADER_L2);
		kill_ps(&p);
		return PS_HEADER_L2;
	} else {
		cwrite_ps(buf, &p, PS_HEADER_L1);
		kill_ps(&p);
		return PS_HEADER_L1;
	}
}


void get_pespts(uint8_t *spts,uint8_t *pts)
{

        pts[0] = 0x21 |
                ((spts[0] & 0xC0) >>5);
        pts[1] = ((spts[0] & 0x3F) << 2) |
                ((spts[1] & 0xC0) >> 6);
        pts[2] = 0x01 | ((spts[1] & 0x3F) << 2) |
                ((spts[2] & 0x80) >> 6);
        pts[3] = ((spts[2] & 0x7F) << 1) |
                ((spts[3] & 0x80) >> 7);
        pts[4] = 0x01 | ((spts[3] & 0x7F) << 1);
}

int write_pes_header(uint8_t id, int length , uint64_t PTS, uint64_t DTS, 
		     uint8_t *obuf, int stuffing, uint8_t ptsdts)
{
	uint8_t le[2];
	uint8_t dummy[3];
	uint8_t *pts;
	uint8_t ppts[5];
	uint32_t lpts;
	uint8_t *dts;
	uint8_t pdts[5];
	uint32_t ldts;
	int c;
	uint8_t headr[3] = {0x00, 0x00, 0x01};
	
	lpts = htonl((PTS/300ULL) & 0x00000000FFFFFFFFULL);
	pts = (uint8_t *) &lpts;
	get_pespts(pts,ppts);
	if ((PTS/300ULL) & 0x0000000100000000ULL) ppts[0] |= 0x80;

	ldts = htonl((DTS/300ULL) & 0x00000000FFFFFFFFULL);
	dts = (uint8_t *) &ldts;
	get_pespts(dts,pdts);
	if ((DTS/300ULL) & 0x0000000100000000ULL) pdts[0] |= 0x80;

	c = 0;
	memcpy(obuf+c,headr,3);
	c += 3;
	memcpy(obuf+c,&id,1);
	c++;

	le[0] = 0;
	le[1] = 0;
	length -= 6;

	le[0] |= ((uint8_t)(length >> 8) & 0xFF); 
	le[1] |= ((uint8_t)(length) & 0xFF); 
	memcpy(obuf+c,le,2);
	c += 2;

	if (id == PADDING_STREAM){
		memset(obuf+c,0xff,length);
		c+= length;
		return c;
	}

	dummy[0] = 0x80;
	dummy[1] = 0;
	dummy[2] = 0;
	
	if (ptsdts == PTS_ONLY){
		dummy[2] = 5 + stuffing;
		dummy[1] |= PTS_ONLY;
	} else 	if (ptsdts == PTS_DTS){
		dummy[2] = 10 + stuffing;
		dummy[1] |= PTS_DTS;
	}

	memcpy(obuf+c,dummy,3);
	c += 3;

	memset(obuf+c,0xFF,stuffing);
	c += stuffing;

	if (ptsdts == PTS_ONLY){
		memcpy(obuf+c,ppts,5);
		c += 5;
	} else if ( ptsdts == PTS_DTS ){
		memcpy(obuf+c,ppts,5);
		c += 5;
		memcpy(obuf+c,pdts,5);
		c += 5;
	}
	return c;
}

void write_padding_pes( int pack_size, int apidn, int ac3n, 
			uint64_t SCR, uint64_t muxr, uint8_t *buf)
{
	int pos = 0;

	pos = write_ps_header(buf,SCR,muxr, apidn+ ac3n, 0, 0, 1, 1, 1,
			      0);

	pos += write_pes_header( PADDING_STREAM, pack_size-pos, 0, 0, buf+pos,
				 0, 0);

}

int write_video_pes( int pack_size, int apidn, int ac3n, uint64_t vpts, 
		     uint64_t vdts, uint64_t SCR, uint64_t muxr, 
		     uint8_t *buf, int *vlength, 
		     uint8_t ptsdts, ringbuffer *vrbuffer)
{
	int add;
	int pos = 0;
	int p   = 0;
	int stuff = 0;
	int length = *vlength;

#ifdef PES_DEBUG
	fprintf(stderr,"write video PES ");
	printpts(vdts);
	fprintf(stderr,"\n");
#endif
	if (! length) return 0;
	p = PS_HEADER_L1+PES_H_MIN;

	if ( ptsdts == PTS_ONLY){
		p += 5;
	} else if (ptsdts == PTS_DTS){
		p += 10;
	}

	if ( length+p >= pack_size){
		length = pack_size;
	} else {
		if (pack_size - length - p <= PES_MIN){
			stuff = pack_size - length-p;
			length = pack_size;
		} else 
			length = length+p;
	}

	pos = write_ps_header(buf,SCR,muxr, apidn+ac3n, 0, 0, 1, 1, 
			      1, 0);

	pos += write_pes_header( 0xE0, length-pos, vpts, vdts, buf+pos, 
				 stuff, ptsdts);
	if (length-pos > *vlength){
		fprintf(stderr,"WHAT THE HELL  %d > %d\n", length-pos,
			*vlength);
	}

	add = ring_read( vrbuffer, buf+pos, length-pos);
	*vlength = add;
	if (add < 0) return -1;
	pos += add;

	if (pos+PES_MIN < pack_size){
		pos += write_pes_header( PADDING_STREAM, pack_size-pos, 0, 0,
					 buf+pos, 0, 0);
		pos = pack_size;
	}		
	return pos;
}

int write_audio_pes(  int pack_size, int apidn, int ac3n, int n, uint64_t pts, 
		      uint64_t SCR, uint32_t muxr, uint8_t *buf, int *alength, 
		      uint8_t ptsdts, 	ringbuffer *arbuffer)
{
	int add;
	int pos = 0;
	int p   = 0;
	int stuff = 0;
	int length = *alength;

#ifdef PES_DEBUG
	fprintf(stderr,"write audio PES ");
	printpts(pts);
	fprintf(stderr,"\n");
#endif

	if (!length) return 0;
	p = PS_HEADER_L1+PES_H_MIN;

	if (ptsdts == PTS_ONLY){
		p += 5;
	}

	if ( length+p >= pack_size){
		length = pack_size;
	} else {
		if (pack_size-length-p <= PES_MIN){
			stuff = pack_size - length-p;
			length = pack_size;
		} else 
			length = length+p;
	}
	pos = write_ps_header(buf,SCR,muxr, apidn + ac3n, 0, 0, 1, 1, 
			      1, 0);
	pos += write_pes_header( 0xC0+n, length-pos, pts, 0, buf+pos, stuff, 
				 ptsdts);
	add = ring_read( arbuffer, buf+pos, length-pos);
	*alength = add;
	if (add < 0) return -1;
	pos += add;

	if (pos+PES_MIN < pack_size){
		pos += write_pes_header( PADDING_STREAM, pack_size-pos, 0,0,
					 buf+pos, 0, 0);
		pos = pack_size;
	}		
	if (pos != pack_size) {
		fprintf(stderr,"apos: %d\n",pos);
		exit(1);
	}

	return pos;
}



int write_ac3_pes(  int pack_size, int apidn, int ac3n, int n,
		    uint64_t pts, uint64_t SCR, 
		    uint32_t muxr, uint8_t *buf, int *alength, uint8_t ptsdts,
		    int nframes,int ac3_off, ringbuffer *ac3rbuffer, int framelength)
{
	int add;
	int pos = 0;
	int p   = 0;
	int stuff = 0;
	int length = *alength;

#ifdef PES_DEBUG
	fprintf(stderr,"write ac3 PES ");
	printpts(pts);
	fprintf(stderr,"\n");
#endif

	if (!length) return 0;
	p = PS_HEADER_L1+PES_H_MIN;

	if (ptsdts == PTS_ONLY){
		p += 5;
	}

	if ( length+p >= pack_size){
		if (length+p -pack_size == framelength-4) nframes--;
		length = pack_size;
	} else {
		if (pack_size-length-p <= PES_MIN){
			stuff = pack_size - length-p;
			length = pack_size;
		} else 
			length = length+p;
	}
	pos = write_ps_header(buf,SCR,muxr, apidn+ac3n, 0, 0, 1, 1, 
			      1, 0);

	pos += write_pes_header( PRIVATE_STREAM1, length-pos, pts, 0, 
				 buf+pos, stuff, ptsdts);
	buf[pos] = 0x80 + n +apidn;
	buf[pos+1] = nframes;
	buf[pos+2] = (ac3_off >> 8)& 0xFF;
	buf[pos+3] = (ac3_off)& 0xFF;
	pos += 4;

	add = ring_read( ac3rbuffer, buf+pos, length-pos);
	*alength = add;
	if (add < 0) return -1;
	pos += add;

	if (pos+PES_MIN < pack_size){
		pos += write_pes_header( PADDING_STREAM, pack_size-pos, 0,0,
					 buf+pos, 0, 0);
		pos = pack_size;
	}		
	if (pos != pack_size) {
		fprintf(stderr,"apos: %d\n",pos);
		exit(1);
	}

	return pos;
}


int bwrite_audio_pes(  int pack_size, int apidn, int ac3n, int n, uint64_t pts, 
		      uint64_t SCR, uint32_t muxr, uint8_t *buf, int *alength, 
		       uint8_t ptsdts, uint8_t *arbuffer, int bsize )
{
	int add;
	int pos = 0;
	int p   = 0;
	int stuff = 0;
	int length = *alength;

#ifdef PES_DEBUG
	fprintf(stderr,"write audio PES ");
	printpts(pts);
	fprintf(stderr,"\n");
#endif

	if (!length) return 0;
	p = PS_HEADER_L1+PES_H_MIN;

	if (ptsdts == PTS_ONLY){
		p += 5;
	}

	if ( length+p >= pack_size){
		length = pack_size;
	} else {
		if (pack_size-length-p <= PES_MIN){
			stuff = pack_size - length-p;
			length = pack_size;
		} else 
			length = length+p;
	}
	pos = write_ps_header(buf,SCR,muxr, apidn + ac3n, 0, 0, 1, 1, 
			      1, 0);
	pos += write_pes_header( 0xC0+n, length-pos, pts, 0, buf+pos, stuff, 
				 ptsdts);

	if (length -pos < bsize){
		memcpy(buf+pos, arbuffer, length-pos);
		add = length - pos;
		*alength = add;
	} else  return -1;
	
	pos += add;

	if (pos+PES_MIN < pack_size){
		pos += write_pes_header( PADDING_STREAM, pack_size-pos, 0,0,
					 buf+pos, 0, 0);
		pos = pack_size;
	}		
	if (pos != pack_size) {
		fprintf(stderr,"apos: %d\n",pos);
		exit(1);
	}

	return pos;
}



int bwrite_ac3_pes(  int pack_size, int apidn, int ac3n, int n,
		    uint64_t pts, uint64_t SCR, 
		    uint32_t muxr, uint8_t *buf, int *alength, uint8_t ptsdts,
		     int nframes,int ac3_off, uint8_t *ac3rbuffer, int bsize, int framelength)
{
	int add;
	int pos = 0;
	int p   = 0;
	int stuff = 0;
	int length = *alength;

#ifdef PES_DEBUG
	fprintf(stderr,"write ac3 PES ");
	printpts(pts);
	fprintf(stderr,"\n");
#endif
	if (!length) return 0;
	p = PS_HEADER_L1+PES_H_MIN;

	if (ptsdts == PTS_ONLY){
		p += 5;
	}

	if ( length+p >= pack_size){
		if (length+p -pack_size == framelength-4) nframes--;
		length = pack_size;
	} else {
		if (pack_size-length-p <= PES_MIN){
			stuff = pack_size - length-p;
			length = pack_size;
		} else 
			length = length+p;
	}
	pos = write_ps_header(buf,SCR,muxr, apidn+ac3n, 0, 0, 1, 1, 
			      1, 0);

	pos += write_pes_header( PRIVATE_STREAM1, length-pos, pts, 0, 
				 buf+pos, stuff, ptsdts);
	buf[pos] = 0x80 + n +apidn;
	buf[pos+1] = nframes;
	buf[pos+2] = (ac3_off >> 8)& 0xFF;
	buf[pos+3] = (ac3_off)& 0xFF;
	pos += 4;

	if (length-pos <= bsize){
		memcpy(buf+pos, ac3rbuffer, length-pos);
		add = length-pos;
		*alength = add;
	} else return -1;
	pos += add;

	if (pos+PES_MIN < pack_size){
		pos += write_pes_header( PADDING_STREAM, pack_size-pos, 0,0,
					 buf+pos, 0, 0);
		pos = pack_size;
	}		
	if (pos != pack_size) {
		fprintf(stderr,"apos: %d\n",pos);
		exit(1);
	}

	return pos;
}


int write_nav_pack(int pack_size, int apidn, int ac3n, uint64_t SCR, uint32_t muxr, 
		   uint8_t *buf)
{
	int pos = 0;
        uint8_t headr[5] = {0x00, 0x00, 0x01, PRIVATE_STREAM2, 0x03 };

	pos = write_ps_header( buf, SCR, muxr, apidn+ac3n, 0, 0, 1, 1, 1, 1);
	memcpy(buf+pos, headr, 5);
	buf[pos+5] = 0xD4;
	pos += 6;
	memset(buf+pos, 0, 0x03d4);
	pos += 0x03d4;

	memcpy(buf+pos, headr, 5);
	buf[pos+5] = 0xFA;
	pos += 6;
	memset(buf+pos, 0, 0x03fA);
	pos += 0x03fA;
	
	return pos;
}
