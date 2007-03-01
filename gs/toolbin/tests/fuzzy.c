/**
 * Fuzzy comparison utility. Copyright 2001-2003 artofcode LLC.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sys/stat.h>

typedef unsigned char uchar;
typedef int bool;
#define FALSE 0
#define TRUE 1

#define BMP_FILE_HEADER_SIZE 14
#define BMP_INFO_HEADER_SIZE 40
#define BMP_HEADER_SIZE ((BMP_FILE_HEADER_SIZE) + \
				(BMP_INFO_HEADER_SIZE))

#define MIN(x,y) ((x)>(y)?(y):(x))
#define MAX(x,y) ((x)>(y)?(x):(y))

typedef struct _Image Image;

struct _Image {
  int (*close) (Image *self);
  int (*get_scan_line) (Image *self, uchar *buf);
  int (*seek) (Image *self, int y);
  int (*feof_) (Image *self);
  int width;
  int height;
  int maxval;
  int n_chan;
  int raster;
  int bpp; /* bits per pixel */
};

typedef struct _FuzzyParams FuzzyParams;

struct _FuzzyParams {
  int tolerance;    /* in pixel counts */
  int window_size;
  bool report_coordinates;
};

typedef struct _FuzzyReport FuzzyReport;

struct _FuzzyReport {
  int n_diff;
  int n_outof_tolerance;
  int n_outof_window;
};

static off_t
file_length(int file)
{
  struct stat st;

  fstat(file, &st);
  return st.st_size;
}

int
image_get_rgb_scan_line (Image *image, uchar *buf)
{
  uchar *image_buf;
  int width = image->width;
  int code;
  int x;

  if (image->n_chan == 3 && image->bpp == 8)
    return image->get_scan_line (image, buf);

  image_buf = malloc (image->raster);
  code = image->get_scan_line (image, image_buf);
  if (code < 0)
    {
      /* skip */
    }
  else if (image->n_chan == 1 && image->bpp == 8)
    {
      for (x = 0; x < width; x++)
	{
	  uchar g = image_buf[x];
	  buf[x * 3] = g;
	  buf[x * 3 + 1] = g;
	  buf[x * 3 + 2] = g;
	}
    }
  else if (image->n_chan == 1 && image->bpp == 1)
    {
      for (x = 0; x < width; x++)
	{
	  uchar g = -!(image_buf[x >> 3] && (128 >> (x & 7)));
	  buf[x * 3] = g;
	  buf[x * 3 + 1] = g;
	  buf[x * 3 + 2] = g;
	}
    }
  else
    code = -1;
  free (image_buf);
  return code;
}
int
image_get_rgb_scan_line_with_error (Image *image, uchar *buf, int image_index, int row_index, int *rcode)
{
    int code = image_get_rgb_scan_line (image, buf);

    if (code == 1) {
	*rcode = 1;
	printf("*** I/O error in file %d at y = %d\n", image_index, row_index);
    }
    return code;
}

int
image_close (Image *image)
{
  return image->close (image);
}

static int
no_seek(Image *self, int y)
{
  return 0;
}


typedef struct _ImagePnm ImagePnm;

struct _ImagePnm {
  Image super;
  FILE *f;
  long file_length;
};

static int
image_pnm_close (Image *self)
{
  ImagePnm *pnm = (ImagePnm *)self;
  int code;

  code = fclose (pnm->f);
  free (self);
  return code;
}

static ImagePnm *
create_pnm_image_struct(Image *templ, const char *path)
{
  FILE *f = fopen(path,"w+b");
  ImagePnm *pnm = (ImagePnm *)malloc(sizeof(ImagePnm));

  if (pnm == NULL) {
    printf ("Insufficient RAM.\n");
    return NULL;
  }
  if (f == NULL) {
    printf ("Can't create the file %s\n", path);
    return NULL;
  }
  pnm->f = f;
  pnm->super = *templ;
  pnm->super.seek = no_seek;
  pnm->super.bpp = 8;    /* Now support this value only. */
  pnm->super.n_chan = 3; /* Now support this value only. */
  return pnm;
}

static ImagePnm *
create_pnm_image(Image *templ, const char *path)
{
  ImagePnm *pnm = create_pnm_image_struct(templ, path);

  if (pnm == NULL)
    return NULL;

  fprintf(pnm->f,"P6\n");
  fprintf(pnm->f,"# Generated by Ghostscript's fuzzy.c\n");
  fprintf(pnm->f,"%d %d\n", templ->width, pnm->super.height);
  fprintf(pnm->f,"255\n");
  return pnm;
}

static int
image_pnm_feof (Image *self)
{
  ImagePnm *pnm = (ImagePnm *)self;

  return feof (pnm->f) || ftell(pnm->f) == pnm->file_length;
}

static void
write_int16(short v, FILE *f)
{
  uchar val[2];
  val[0] = (uchar)(v & 0xff);
  val[1] = (uchar)((v >> 8) & 0xff);
  fwrite(val, 2, 1, f);
}

static void
write_int32(int v, FILE *f)
{
  uchar val[4];
  val[0] = (uchar)(v & 0xff);
  val[1] = (uchar)((v >> 8) & 0xff);
  val[2] = (uchar)((v >> 16) & 0xff);
  val[3] = (uchar)((v >> 24) & 0xff);
  fwrite(val, 4, 1, f);
}
    
static int
seek_bmp_image(Image *self, int y)
{
  ImagePnm *pnm = (ImagePnm *)self;
  long pos = BMP_HEADER_SIZE + self->raster * (self->height - y - 1);
  int r = fseek(pnm->f, pos, SEEK_SET);

  return r;
}

static ImagePnm *
create_bmp_image(Image *templ, const char *path)
{
  int raster = (templ->width * 3 + 3) & ~3;
  int nImageSize = raster * templ->height;
  int nFileSize = BMP_HEADER_SIZE + nImageSize;
  ImagePnm *pnm = create_pnm_image_struct(templ, path);

  if (pnm == NULL)
    return NULL;
  pnm->super.seek = seek_bmp_image;
  pnm->super.raster = raster;

  /* BMP file header */
  fputc('B', pnm->f);
  fputc('M', pnm->f);
  write_int32(nFileSize, pnm->f);
  write_int16(0, pnm->f); /* reserved fields are zero */
  write_int16(0, pnm->f);
  write_int32(BMP_HEADER_SIZE, pnm->f);

  /* BMP info header */
  write_int32(BMP_INFO_HEADER_SIZE, pnm->f);
  write_int32(templ->width, pnm->f);
  write_int32(pnm->super.height, pnm->f);
  write_int16(1, pnm->f);	/* planes */
  write_int16(24, pnm->f);	/* bit count */
  write_int32(0, pnm->f);	/* compression */
  write_int32(0, pnm->f);	/* size image */
  write_int32(3780, pnm->f);	/* resolution in pixels per meter */
  write_int32(3780, pnm->f);	/* use a default 96 dpi */
  write_int32(0, pnm->f);	/* ClrUsed */
  write_int32(0, pnm->f);	/* ClrImportant */

  /* fseek beyond file end doesn't work with MSVC so we pad
     out to the size of the image data at file open time.    */
  {
    uchar *linebuf = malloc(raster);
    if (linebuf == NULL) {
        printf ("Couldn't allocate dummy bmp line buffer; diff image may be corrupt.\n");
    } else {
	int k;

        memset(linebuf, 0, raster);
        
	for (k = 0; k < pnm->super.height; k++)
	    fwrite(linebuf, 1, raster, pnm->f);
        
        free(linebuf);
    }
  }
  
  return pnm;
}


static int
image_pnm_get_scan_line (Image *self, uchar *buf)
{
  ImagePnm *pnm = (ImagePnm *)self;
  int n_bytes = self->raster;
  int code;
  int bppval = (1 << self->bpp) -1;
  int maxval = self->maxval;

  code = fread (buf, 1, n_bytes, pnm->f);
  if (maxval < bppval) {
      int i;
      for(i = 0; i<n_bytes; i++)
        buf[i] = buf[i] * bppval / maxval;
  }
  return (code < n_bytes) ? -1 : 0;
}

ImagePnm *
alloc_pnm_image (const char *fn)
{
  ImagePnm *image;

  FILE *f = fopen (fn, "rb");

  if (f == NULL) {
      printf("Can't open file %s\n", fn);
      return NULL;
  }
  image = (ImagePnm *)malloc (sizeof(ImagePnm));
  image->f = f;
  return image;
}

int
open_pnm_image (ImagePnm *image)
{
  int width, height;
  int maxval = 0;
  int n_chan, bpp;
  char linebuf[256];

  if (fgets (linebuf, sizeof(linebuf), image->f) == NULL ||
      linebuf[0] != 'P' || linebuf[1] < '4' || linebuf[1] > '6')
    return 1;
  switch (linebuf[1])
    {
    case '4':
      n_chan = 1;
      bpp = 1;
      maxval = 1;
      break;
    case '5':
      n_chan = 1;
      bpp = 8;
      break;
    case '6':
      n_chan = 3;
      bpp = 8;
      break;
    default:
      return 1;
    }
  do
    {
      if (fgets (linebuf, sizeof(linebuf), image->f) == NULL)
	  return 1;
    }
  while (linebuf[0] == '#');
  if (sscanf (linebuf, "%d %d", &width, &height) != 2)
      return 1;
  while (!maxval)
    {
      if (fgets (linebuf, sizeof(linebuf), image->f) == NULL)
	  return 1;
      if (linebuf[0] == '#')
          continue; 
      if (sscanf(linebuf, "%d", &maxval) != 1 || maxval <= 0 || maxval > 255)
	  return 1;
    }
  image->super.width = width;
  image->super.height = height;
  image->super.maxval = maxval;
  image->super.raster = n_chan * ((width * bpp + 7) >> 3);
  image->super.n_chan = n_chan;
  image->super.bpp = bpp;
  image->file_length = file_length(fileno(image->f));
  return 0;
}

Image *
alloc_image_file (const char *fn)
{
  /* This is the place to add a dispatcher for other image types. */
    ImagePnm *image = alloc_pnm_image (fn);

  if (image == NULL)
    return NULL;
  image->super.close = image_pnm_close;
  image->super.get_scan_line = image_pnm_get_scan_line;
  image->super.seek = no_seek;
  image->super.feof_ = image_pnm_feof;
  return &image->super;
}

static int
open_image (Image *self)
{
  /* This is the place to add a dispatcher for other image types. */
  ImagePnm *pnm = (ImagePnm *)self;

  return open_pnm_image (pnm);
}

static uchar **
alloc_window (int row_bytes, int window_size)
{
  uchar **buf = (uchar **)malloc (window_size * sizeof(uchar *));
  int i;

  for (i = 0; i < window_size; i++)
    buf[i] = malloc (row_bytes);
  return buf;
}

static void
free_window (uchar **buf, int window_size)
{
  int i;

  for (i = 0; i < window_size; i++)
    free (buf[i]);
  free (buf);
}

static void
roll_window (uchar **buf, int window_size)
{
  int i;
  uchar *tmp1, *tmp2;

  tmp1 = buf[window_size - 1];
  for (i = 0; i < window_size; i++)
    {
      tmp2 = buf[i];
      buf[i] = tmp1;
      tmp1 = tmp2;
    }
}

static bool
check_window (uchar **buf1, uchar **buf2,
	      const FuzzyParams *fparams,
	      int x, int y, int width, int height)
{
  int tolerance = fparams->tolerance;
  int window_size = fparams->window_size;
  int i, j;
  const int half_win = window_size >> 1;
  const uchar *rowmid1 = buf1[half_win];
  const uchar *rowmid2 = buf2[half_win];
  uchar r1 = rowmid1[x * 3], g1 = rowmid1[x * 3 + 1], b1 = rowmid1[x * 3 + 2];
  uchar r2 = rowmid2[x * 3], g2 = rowmid2[x * 3 + 1], b2 = rowmid2[x * 3 + 2];
  bool match1 = FALSE, match2 = FALSE;

  for (j = -half_win; j <= half_win; j++)
    {
      const uchar *row1 = buf1[j + half_win];
      const uchar *row2 = buf2[j + half_win];
      for (i = -half_win; i <= half_win; i++)
	if ((i != 0 || j != 0) &&
	    x + i >= 0 && x + i < width &&
	    y + j >= 0 && y + j < height)
	  {
	    if (abs (r1 - row2[(x + i) * 3]) <= tolerance &&
		abs (g1 - row2[(x + i) * 3 + 1]) <= tolerance &&
		abs (b1 - row2[(x + i) * 3 + 2]) <= tolerance)
	      match1 = TRUE;
	    if (abs (r2 - row1[(x + i) * 3]) <= tolerance &&
		abs (g2 - row1[(x + i) * 3 + 1]) <= tolerance &&
		abs (b2 - row1[(x + i) * 3 + 2]) <= tolerance)
	      match2 = TRUE;
	  }
    }
  return !(match1 && match2);
}

static int
fuzzy_diff_images (Image *image1, Image *image2, const FuzzyParams *fparams,
		   FuzzyReport *freport, ImagePnm *image_out)
{
  int width = MIN(image1->width, image2->width);
  int height = MIN(image1->height, image2->height);
  int max_height = MAX(image1->height, image2->height);
  int tolerance = fparams->tolerance;
  int window_size = fparams->window_size;
  int row_bytes = width * 3;
  unsigned int out_buffer_size = (image_out ? row_bytes : 0);
  int half_win = window_size >> 1;
  uchar **buf1 = alloc_window (row_bytes, window_size);
  uchar **buf2 = alloc_window (row_bytes, window_size);
  uchar *out_buf = NULL;
  int x0 = -2, y0 = -2, mc = 0, mmax = 10;
  int y;
  int rcode = 0;

  if (image_out != NULL)
    {
      out_buf = malloc(out_buffer_size);
      if (out_buf == NULL) 
        printf ("Can't allocate output buffer.\n");
    }

  /* Read rows ahead for half window : */
  for (y = 0; y < MIN(half_win, height); y++) 
    {
       if (image_get_rgb_scan_line_with_error (image1, buf1[half_win - y], 1, y, &rcode))
	    goto ex;
       if (image_get_rgb_scan_line_with_error (image2, buf2[half_win - y], 2, y, &rcode))
	    goto ex;
    }
  /* Initialie remaining scanlines if height < half_win */
  for (; y < half_win; y++) {
    memcpy(buf1[half_win - y], buf1[half_win], width * 3); 
    memcpy(buf2[half_win - y], buf2[half_win], width * 3);
  }

  /* Initialie the rest of the buffer that would be uninitialised */
  /* before half_win lines is read, because check_window() always */
  /* peeks into the whole window */
  for (y = 0; y < half_win; y++) {
    memcpy(buf1[half_win + y + 1], buf1[half_win], width * 3);
    memcpy(buf2[half_win + y + 1], buf2[half_win], width * 3);
  }

  /* Do compare : */
  freport->n_diff = 0;
  freport->n_outof_tolerance = 0;
  freport->n_outof_window = 0;

  for (y = 0; y < height; y++)
    {
      int x;
      uchar *row1 = buf1[0];
      uchar *row2 = buf2[0];
      uchar *rowmid1 = buf1[half_win];
      uchar *rowmid2 = buf2[half_win];

      if (y < height - half_win)
        {
	    if (image_get_rgb_scan_line_with_error (image1, row1, 1, y + half_win, &rcode))
		goto ex;
	    if (image_get_rgb_scan_line_with_error (image2, row2, 2, y + half_win, &rcode))
		goto ex;
        }
      if (out_buf)
	memset(out_buf, 0, out_buffer_size);

      if (memcmp(rowmid1, rowmid2, width * 3)) 
        {
          for (x = 0; x < width; x++)
	    {
	      if (rowmid1[x * 3] != rowmid2[x * 3] ||
	          rowmid1[x * 3 + 1] != rowmid2[x * 3 + 1] ||
	          rowmid1[x * 3 + 2] != rowmid2[x * 3 + 2])
	        {
	          freport->n_diff++;
	          if (abs (rowmid1[x * 3] - rowmid2[x * 3]) > tolerance ||
		      abs (rowmid1[x * 3 + 1] - rowmid2[x * 3 + 1]) > tolerance ||
		      abs (rowmid1[x * 3 + 2] - rowmid2[x * 3 + 2]) > tolerance)
		    {
		      freport->n_outof_tolerance++;
		      if (check_window (buf1, buf2, fparams, x, y, width, height)) {
		        freport->n_outof_window++;
		        if (out_buf && x < image1->width) {
		          out_buf[x * 3 + 0] = abs(rowmid1[x * 3 + 0]- rowmid2[x * 3 + 0]);
		          out_buf[x * 3 + 1] = abs(rowmid1[x * 3 + 1]- rowmid2[x * 3 + 1]);
		          out_buf[x * 3 + 2] = abs(rowmid1[x * 3 + 2]- rowmid2[x * 3 + 2]);
		        }
		        if (fparams->report_coordinates && 
			    (abs(x - x0) > 1 && y == y0 || y - y0 > 1))
		          {
		            /* fixme : a contiguity test wanted. */
			    x0 = x; y0 = y;
			    mc++;
			    if (mc < mmax) {
			        printf("diff: x=%d y=%d c1=%02X%02X%02X c2=%02X%02X%02X\n", x, y,
			    	        rowmid1[x * 3], rowmid1[x * 3 + 1], rowmid1[x * 3 + 2],
				        rowmid2[x * 3], rowmid2[x * 3 + 1], rowmid2[x * 3 + 2]);
			    } else if (mc == mmax)
			        printf("Won't report more differences.\n");
		        }
		      }
		    }
	        }
	    }
        }

      roll_window (buf1, window_size);
      roll_window (buf2, window_size);
      if (out_buf) {
        if (image_out->super.seek(&image_out->super, y))
	  {
	    printf ("I/O Error seeking to the output image position.\n");
	    free(out_buf);
	    out_buf = NULL;
	  }
	else if (fwrite(out_buf, 1, out_buffer_size, image_out->f) != out_buffer_size)
	  {
	    printf ("I/O Error writing the output image.\n");
	    free(out_buf);
	    out_buf = NULL;
	  }
      }
    }
  y += half_win;
  for (; y < max_height; y++) {
      if (y < image1->height) {
	    if (image_get_rgb_scan_line_with_error(image1, buf1[0], 1, y, &rcode))
		goto ex;
      }
      if (y < image2->height) {
	    if (image_get_rgb_scan_line_with_error(image2, buf2[0], 2, y, &rcode))
		goto ex;
      }
  }
ex:
  free_window (buf1, window_size);
  free_window (buf2, window_size);
  if (out_buf)
    free(out_buf);
  if (image_out)
    fclose(image_out->f);
  return rcode;
}

static const char *
get_arg (int argc, char **argv, int *pi, const char *arg)
{
  if (arg[0] != 0)
    return arg;
  else
    {
      (*pi)++;
      if (*pi == argc)
	return NULL;
      else
	return argv[*pi];
    }
}

int
usage (void)
{
  printf ("Usage: fuzzy [-w window_size] [-t tolerance] [-c] a.ppm b.ppm [diff.ppm | diff.bmp]\n");
  return 1;
}

int
main (int argc, char **argv)
{
  Image *image1,  *image2;
  ImagePnm *image_out = NULL;
  FuzzyParams fparams;
  FuzzyReport freport;
  const char *fn[3] = {0, 0, 0};
  int fn_idx = 0;
  int i, page = 1, rcode = 0;
  char *out_fn = NULL;

  fparams.tolerance = 2;
  fparams.window_size = 3;
  fparams.report_coordinates = FALSE;

  for (i = 1; i < argc; i++)
    {
      const char *arg = argv[i];

      if (arg[0] == '-')
	{
	  switch (arg[1])
	    {
	    case 'w':
	      fparams.window_size = atoi (get_arg (argc, argv, &i, arg + 2));
	      if ((fparams.window_size & 1) == 0)
		{
		  printf ("window size must be odd\n");
		  return 1;
		}
	      break;
	    case 't':
	      fparams.tolerance = atoi (get_arg (argc, argv, &i, arg + 2));
	      break;
	    case 'c':
	      fparams.report_coordinates = TRUE;
	      break;
	    default:
	      return usage ();
	    }
	}
      else if (fn_idx < sizeof(fn)/sizeof(fn[0]))
	fn[fn_idx++] = argv[i];
      else
	return usage ();
    }

  if (fn_idx < 2)
    return usage ();

  image1 = alloc_image_file (fn[0]);
  if (image1 == NULL)
      return 1;

  image2 = alloc_image_file (fn[1]);
  if (image2 == NULL)
    {
      image_close (image1);
      return 1;
    }

  if (fn[2]) {
      out_fn = malloc(strlen(fn[2]) + 5);
      if (out_fn == NULL) {
	  printf("Can't alloc the output file name.\n");
	  return 1;
      }
  }

  while (!image1->feof_(image1) || !image2->feof_(image2)) 
  { 
    if (image1->feof_(image1)) 
    {
      printf ("Extra data (maybe pages) in the image file 2.\n");
      return 1;
    }
    if (image2->feof_(image2)) 
    {
      printf ("Extra data (maybe pages) in the image file 1.\n");
      return 1;
    }
    if (open_image(image1))
    {
      printf ("Error parsing the page %d header in file %s\n", page, fn[0]);
      return 1;
    }

    if (open_image(image2))
    {
      printf ("Error parsing the page %d header in file %s\n", page, fn[1]);
      return 1;
    }
    if (image1->width != image2->width) {
	printf("Diffenert image width for page %d\n", page);
	rcode = MAX(rcode, 1);
    }
    if (image1->height != image2->height) {
	printf("Diffenert image height for page %d\n", page);
	rcode = MAX(rcode, 1);
    }
    if (out_fn != NULL) {
      int l = strlen(fn[2]);

      strcpy(out_fn, fn[2]);
      for (i = l; i >= 0; i--) {
	  if (out_fn[i] == '\\' || out_fn[i] == '/' || out_fn[i] == '.')
	      break;
      }
      if (out_fn[i] == '.') {
	  char c;

	  memmove(out_fn + i + 4, out_fn + i, l + 1 - i);
	  c = out_fn[i + 4];
	  sprintf(out_fn + i, "-%03d", page);
	  out_fn[i + 4] = c;
      } else
	  sprintf(out_fn + l, "-%03d", page);
      image_out = 
       (!strcmp(fn[2]+ strlen(fn[2]) - 4, ".bmp") ? create_bmp_image
                                                  : create_pnm_image)
           (image1, out_fn);
    } else
      image_out = NULL;
    if (fuzzy_diff_images (image1, image2, &fparams, &freport, image_out))
	return 1;
    if (image_out)
      image_pnm_close (&image_out->super);
    image_out = NULL;
    if (freport.n_diff > 0)
    {
      printf ("%s: page %d: %d different, %d out of tolerance, %d out of window\n",
 	      fn[0], page, freport.n_diff, freport.n_outof_tolerance,
	      freport.n_outof_window);
      rcode = MAX(rcode, 1);
    }
    if (freport.n_outof_window > 0)
      rcode = MAX(rcode, 2);
    page++;
  }
  image_close (image1);
  image_close (image2);
  return rcode;
}
