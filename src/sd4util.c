/***********************************************************************
      LIBRARY: JPEGL - Lossless JPEG Image Compression

      FILE:    SD4UTIL.C
      AUTHOR:  Craig Watson
      DATE:    12/15/2000

      Contains routines responsible for decoding an old image format
      used for JPEGL-compressing images in NIST Special Database 4.
      This format should be considered obsolete.

      ROUTINES:
#cat: jpegl_sd4_decode_mem - Decompresses a JPEGL-compressed datastream
#cat:           according to the old image format used in NIST Special
#cat:           Database 4.  This routine should be used to decompress
#cat:           legacy data only.  This old format should be considered
#cat:           obsolete.

***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <jpeglsd4.h>
static int getc_huffman_table_jpegl_sd4(HUF_TABLE **, unsigned char **,
                 unsigned char *);
static int decode_data_jpegl_sd4(int *, int *, int *, int *,
                 unsigned char *, unsigned char **, unsigned char *, int *);
static int getc_nextbits_jpegl_sd4(unsigned short *, unsigned char **,
                 unsigned char *, int *, const int);
#include <dataio.h>

/************************************************************************/
/*                        Algorithms coded from:                        */
/*                                                                      */
/*                            Committee draft ISO/IEC CD 10198-1 for    */
/*                            "Digital Compression and Coding of        */
/*                             Continuous-tone Still images"            */
/*                                                                      */
/************************************************************************/
int jpegl_sd4_decode_mem(unsigned char *idata, const int ilen, const int width,
                     const int height, const int depth, unsigned char *odata)
{
   HUF_TABLE *huf_table[MAX_CMPNTS]; /*These match the jpegl static
                                           allocation but jpegl_sd4 only has
                                           1 plane of data not MAX_CMPNTS*/
   int i, ret;
   unsigned char *cbufptr, *ebufptr;
   unsigned char predictor;                /*predictor type used*/
   unsigned char Pt = 0;                   /*Point Transform*/
   /*holds the code for all possible difference values */
   /*that occur when encoding*/
   int huff_decoder[MAX_CATEGORY][LARGESTDIFF+1];
                                                    
   int diff_cat;             /*code word category*/
   int bit_count = 0;        /*marks the bit to receive from the input byte*/
   unsigned short diff_code; /*"raw" difference pixel*/
   int full_diff_code;       /*difference code extend to full precision*/
   short data_pred;          /*prediction of pixel value*/
   unsigned char *outbuf;

   /* Set memory buffer pointers. */
   cbufptr = idata;
   ebufptr = idata + ilen;
   outbuf = odata;


   for(i = 0; i < MAX_CMPNTS; i++)
      huf_table[i] = (HUF_TABLE *)NULL;


   ret = getc_huffman_table_jpegl_sd4(huf_table, &cbufptr, ebufptr);
   if(ret)
      return(ret);

   ret = getc_byte(&predictor, &cbufptr, ebufptr);
   if(ret) {
      free_HUFF_TABLES(huf_table, 1);
      return(ret);
   }

				/*this routine builds a table used in
				  decoding coded difference pixels*/
   build_huff_decode_table(huff_decoder);

				/*decompress the pixel "differences"
				  sequentially*/
   for(i = 0; i < width*height; i++) {

      			        /*get next huffman category code from
				  compressed input data stream*/
      ret = decode_data_jpegl_sd4(&diff_cat, huf_table[0]->mincode,
		      huf_table[0]->maxcode, huf_table[0]->valptr,
		      huf_table[0]->values, &cbufptr, ebufptr, &bit_count);
      if(ret){
         free_HUFF_TABLES(huf_table, 1);
         return(ret);
      }

				/*get the required bits (given by huffman
				  code to reconstruct the difference
				  value for the pixel*/
      ret = getc_nextbits_jpegl_sd4(&diff_code, &cbufptr,
		      ebufptr, &bit_count, diff_cat);
      if(ret){
         free_HUFF_TABLES(huf_table, 1);
         return(ret);
      }

				/*extend the difference value to
				  full precision*/
      full_diff_code = huff_decoder[diff_cat][diff_code];

				/*reverse the pixel prediction and
				  store the pixel value in the
				  output buffer*/
      ret = predict(&data_pred, outbuf, width, i, depth, predictor, Pt);
      if(ret){
         free_HUFF_TABLES(huf_table, 1);
         return(ret);
      }

      *outbuf = full_diff_code + data_pred;
      outbuf++;
   }
   free_HUFF_TABLES(huf_table, 1);

   return(0);
}


/************************************/
/*routine to get huffman code tables*/
/************************************/
static int getc_huffman_table_jpegl_sd4(HUF_TABLE **huf_table,
                        unsigned char **cbufptr, unsigned char *ebufptr)
{
   int i, ret;                  /*increment variable*/
   unsigned char number;               /*number of huffbits and huffvalues*/
   unsigned char *huffbits, *huffvalues;
   HUF_TABLE *thuf_table;

   if(debug > 0)
      fprintf(stdout, "Start reading huffman table jpegl_sd4.\n");

   ret = getc_byte(&number, cbufptr, ebufptr);
   if(ret)
      return(ret);

   huffbits = (unsigned char *)calloc(MAX_HUFFBITS, sizeof(unsigned char));
   if(huffbits == (unsigned char *)NULL){
      fprintf(stderr,
              "ERROR : getc_huffman_table_jpegl_sd4 : calloc : huffbits\n");
      return(-2);
   }

   for (i = 0; i < MAX_HUFFBITS_JPEGL_SD4;  i++)
      ret = getc_byte(&(huffbits[i]), cbufptr, ebufptr);
      if(ret){
         free(huffbits);
         return(ret);
      }

   if(debug > 1)
      for (i = 0; i < MAX_HUFFBITS_JPEGL_SD4;  i++)
         fprintf(stdout, "bits[%d] = %d\n", i, huffbits[i]);

   huffvalues = (unsigned char *)calloc(MAX_HUFFCOUNTS_JPEGL,
                                        sizeof(unsigned char));
   if(huffvalues == (unsigned char *)NULL){
      fprintf(stderr,
              "ERROR : getc_huffman_table_jpegl_sd4 : calloc : huffvalues\n");
      free(huffbits);
      return(-3);
   }
   for (i = 0; i < (number - MAX_HUFFBITS_JPEGL_SD4); i ++)
      ret = getc_byte(&(huffvalues[i]), cbufptr, ebufptr);
      if(ret){
         free(huffbits);
         free(huffvalues);
         return(ret);
      }

   if(debug > 1)
      for (i = 0; i < number-MAX_HUFFBITS_JPEGL_SD4;  i++)
         fprintf(stdout, "values[%d] = %d\n", i, huffvalues[i]);


   thuf_table = (HUF_TABLE *)calloc(1, sizeof(HUF_TABLE));
   if(thuf_table == (HUF_TABLE *)NULL){
      fprintf(stderr, "ERROR : getc_huffman_table_jpegl_sd4 : ");
      fprintf(stderr, "calloc : thuf_table\n");
      return(-4);
   }
   thuf_table->freq = (int *)NULL;
   thuf_table->codesize = (int *)NULL;

   thuf_table->bits = huffbits;
   thuf_table->values = huffvalues;

   huf_table[0] = thuf_table;

   /* Build rest of table. */

   thuf_table->maxcode = (int *)calloc(MAX_HUFFCOUNTS_JPEGL+1,
                                          sizeof(int));
   if(thuf_table->maxcode == (int *)NULL){
      fprintf(stderr, "ERROR : getc_huffman_table_jpegl_sd4 : ");
      fprintf(stderr, "calloc : maxcode\n");
      free_HUFF_TABLE(thuf_table);
      return(-5);
   }

   thuf_table->mincode = (int *)calloc(MAX_HUFFCOUNTS_JPEGL+1,
                                          sizeof(int));
   if(thuf_table->mincode == (int *)NULL){
      fprintf(stderr, "ERROR : getc_huffman_table_jpegl_sd4 : ");
      fprintf(stderr, "calloc : mincode\n");
      free_HUFF_TABLE(thuf_table);
      return(-6);
   }

   thuf_table->valptr = (int *)calloc(MAX_HUFFCOUNTS_JPEGL+1, sizeof(int));
   if(thuf_table->valptr == (int *)NULL){
      fprintf(stderr, "ERROR : getc_huffman_table_jpegl_sd4 : ");
      fprintf(stderr, "calloc : valptr\n");
      free_HUFF_TABLE(thuf_table);
      return(-7);
   }

				/*the next two routines reconstruct
   				  the huffman tables that were used
				  in the Jpeg lossless compression*/
   ret = build_huffsizes(&(thuf_table->huffcode_table),
		   &(thuf_table->last_size), thuf_table->bits,
		   MAX_HUFFCOUNTS_JPEGL);
   if(ret){
      free_HUFF_TABLES(huf_table, 1);
      return(ret);
   }

   build_huffcodes(thuf_table->huffcode_table);

				/*this routine builds a set of three
				  tables used in decoding the compressed
				  data*/
   gen_decode_table(thuf_table->huffcode_table,
                    thuf_table->maxcode, thuf_table->mincode,
                    thuf_table->valptr, thuf_table->bits);

   free(thuf_table->huffcode_table);
   thuf_table->huffcode_table = (HUFFCODE *)NULL;

   if(debug > 0)
      fprintf(stdout, "Done reading huffman table jpegl_sd4.\n");

   return(0);
}

/************************************/
/*routine to decode the encoded data*/
/************************************/
static int decode_data_jpegl_sd4(int *odiff_cat, int *mincode, int *maxcode,
                int *valptr, unsigned char *huffvalues,
                unsigned char **cbufptr, unsigned char *ebufptr,
                int *bit_count)
{
   int ret;
   int inx, inx2;    /*increment variables*/
   int code;         /*becomes a huffman code word one bit at a time*/
   unsigned short tcode, tcode2;
   int diff_cat;     /*category of the huffman code word*/

   ret = getc_nextbits_jpegl_sd4(&tcode, cbufptr, ebufptr, bit_count, 1);
   if(ret)
      return(ret);
   code = tcode;

   for(inx = 1; code > maxcode[inx]; inx++){
      ret = getc_nextbits_jpegl_sd4(&tcode2, cbufptr, ebufptr, bit_count, 1);
      if(ret)
         return(ret);
      code = (code << 1) + tcode2;
   }

   inx2 = valptr[inx];
   inx2 = inx2 + code - mincode[inx];
   diff_cat = huffvalues[inx2];

   *odiff_cat = diff_cat;
   return(0);
}

/**************************************************************/
/*routine to get nextbit(s) of data stream from memory buffer */
/**************************************************************/
static int getc_nextbits_jpegl_sd4(unsigned short *obits,
                  unsigned char **cbufptr, unsigned char *ebufptr,
                  int *bit_count, const int bits_req)
{
   int ret;
   static unsigned char code;    /*next byte of data*/
   unsigned short bits, tbits;   /*bits of current data byte requested*/
   int bits_needed;      /*additional bits required to finish request*/

   /*used to "mask out" n number of bits from data stream*/
   static unsigned char bit_mask[9] = {0x00,0x01,0x03,0x07,0x0f,
                                       0x1f,0x3f,0x7f,0xff};

   if(bits_req == 0){
      *obits = 0;
      return(0);
   }

   if(*bit_count == 0) {
      ret = getc_byte(&code, cbufptr, ebufptr);
      if(ret)
         return(ret);
      *bit_count = BITSPERBYTE;
   }
   if(bits_req <= *bit_count) {
      bits = (code >>(*bit_count - bits_req)) & (bit_mask[bits_req]);
      *bit_count -= bits_req;
      code &= bit_mask[*bit_count];
   }
   else {
      bits_needed = bits_req - *bit_count;
      bits = code << bits_needed;
      *bit_count = 0;
      ret = getc_nextbits_jpegl_sd4(&tbits, cbufptr,
		      ebufptr, bit_count, bits_needed);
      if(ret)
         return(ret);
      bits |= tbits;
   }

   *obits = bits;
   return(0);
}
