#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct
{
	char typ;
	unsigned width, height;
	double x_dpi, y_dpi;
	double x_scale, y_scale;
} img_t;

/*
 * jpeg: { "FF", "D8" };
 * bmp: { "42", "4D" };
 * gif: { "47", "49", "46" };
 * png: { "89", "50", "4E", "47", "0D", "0A", "1A", "0A" };
 */
char img_typ(const char *fn)
{
	FILE *fp;
	char buf[8], typ = 'x';
	if ((fp = fopen(fn, "rb")) == NULL)
	{
		fprintf(stderr, "[ERROR] Unable to open file: %s\n", fn);
		exit(EXIT_FAILURE);
	}
	if (fread(buf, 1, 8, fp) == 8)
	{
		if (((int)(buf[0]&0xFF) == 0x89) && (int)(buf[1]&0xFF) == 0x50)
			typ = 'p';
		else if (((int)(buf[0]&0xFF) == 0x47) && (int)(buf[1]&0xFF) == 0x49)
			typ = 'g';
		else if (((int)(buf[0]&0xFF) == 0x42) && (int)(buf[1]&0xFF) == 0x4D)
			typ = 'b';
		else if (((int)(buf[0]&0xFF) == 0xFF) && (int)(buf[1]&0xFF) == 0xD8)
		{
			fseek(fp, -2, SEEK_CUR);
			if (fread(buf, 1, 4, fp) == 4 && memcmp(buf, "Exif", 4) == 0)
			{
				fseek(fp, 2, SEEK_CUR);
				if (fread(buf, 1, 2, fp) == 2)
				{
					if (memcmp(buf, "II", 2) == 0)
						typ = 'i';
					else if (memcmp(buf, "MM", 2) == 0)
						typ = 'm';
				}
			}
			else
				typ = 'j';
		}
		else if (((int)(buf[0]&0xFF) == 0x49) && (int)(buf[1]&0xFF) == 0x49)
			typ = 't';
		else
			typ = 'x';
	}
	fclose(fp);
	return typ;
}

void png_dim(const char *fn, img_t *png)
{
	char buf[8];
	uint32_t length;
	uint32_t offset;
	char type[4];
	uint32_t width = 0;
	uint32_t height = 0;
	double x_dpi = 96;
	double y_dpi = 96;

	FILE *fp = fopen(fn, "rb");
	if (!fp)
	{
		fprintf(stderr, "ERROR: failed opening [%s]\n", fn);
		exit(1);
	}
	/* check file MAGIC
	if (fread(buf, 1, 8, fp) == 8)
	{
		if (((int)(buf[0]&0xFF) != 0x89) || (int)(buf[1]&0xFF) != 0x50)
		{
			fprintf(stderr, "ERROR: invalid PNG file [%s]\n", fn);
			exit(1);
		}
	}
	*/
	// skip MAGIC
	if (fseek(fp, 8, SEEK_SET))
	{
		fprintf(stderr, "ERROR: failed to seek [%s]\n", fn);
		exit(1);
	}
	// read file content
	while (!feof(fp))
	{
		/* Read the PNG length and type fields for the sub-section. */
		if (fread(&length, sizeof(length), 1, fp) < 1)
			break;
		if (fread(&type, 1, 4, fp) < 4)
			break;
		length = ntohl(length);
		/* The offset for next fseek() is the field length + type length. */
		offset = length + 4;
		if (memcmp(type, "IHDR", 4) == 0)
		{
			if (fread(&width, sizeof(width), 1, fp) < 1)
				break;
			if (fread(&height, sizeof(height), 1, fp) < 1)
				break;
			width = ntohl(width);
			height = ntohl(height);
			/* Reduce the offset by the length of previous freads(). */
			offset -= 8;
		}
		if (memcmp(type, "pHYs", 4) == 0)
		{
			uint32_t x_ppu = 0;
			uint32_t y_ppu = 0;
			uint8_t units = 1;
			if (fread(&x_ppu, sizeof(x_ppu), 1, fp) < 1)
				break;
			if (fread(&y_ppu, sizeof(y_ppu), 1, fp) < 1)
				break;
			if (fread(&units, sizeof(units), 1, fp) < 1)
				break;
			if (units == 1)
			{
				x_ppu = ntohl(x_ppu);
				y_ppu = ntohl(y_ppu);
				x_dpi = (double)x_ppu * 0.0254;
				y_dpi = (double)y_ppu * 0.0254;
			}
			/* Reduce the offset by the length of previous freads(). */
			offset -= 9;
		}
		if (memcmp(type, "IEND", 4) == 0)
			break;
		if (!feof(fp) && fseek(fp, offset, SEEK_CUR))
		{
			fprintf(stderr, "ERROR: fseek error\n");
			exit(1);
		}
	}
	/* Ensure that we read some valid data from the file. */
	if (width == 0)
	{
		fprintf(stderr, "ERROR: invalid width of zero\n");
		exit(1);
	}
	/* Set the image metadata. */
	png->width = width;
	png->height = height;
	png->x_dpi = x_dpi ? x_dpi : 96.0;
	png->y_dpi = y_dpi ? y_dpi : 96.0;
	fclose(fp);
}

void jpeg_dim(const char *fn, img_t *jpeg)
{
	uint16_t length;
	uint16_t marker;
	uint16_t width = 0, height = 0;
	double x_dpi = 96, y_dpi = 96;
	uint8_t units = 1;
	uint16_t x_density = 0, y_density = 0;

	FILE *fp = fopen(fn, "rb");
	if (!fp)
	{
		fprintf(stderr, "ERROR: failed opening [%s]\n", fn);
		exit(1);
	}
	// skip start of image (SOI)
	if (fseek(fp, 2, SEEK_SET))
	{
		fprintf(stderr, "ERROR: failed to seek [%s]\n", fn);
		exit(1);
	}
	while (true)
	{
		while((marker = getc(fp)) != 0xFF);
		while((marker = getc(fp)) == 0xFF);
		if (marker == 0xE0)
		{
			/* xxd -c16 -g1 -u img.jpeg
			 * 0xFF, 0xD8,                     // start of image (SOI)
			 * 0xFF, 0xE0,                     // APP0 segment
			 * 0x00, 0x10,                     // size of segment, including these 2 bytes; 0x10 = 16 bytes
			 * 0x4A, 0x46, 0x49, 0x46, 0x00,   // identifier string: "JFIF"
			 * 0x01, 0x01,                     // JFIF version 1.01
			 * 0x00,                           // density units (0=no units)
			 * 0x00, 0x01,                     // horizontal density
			 * 0x00, 0x01,                     // vertical density
			 * 0x00,                           // X thumbnail size
			 * 0x00                            // Y thumbnail size
			 */
			fseek(fp, 9, SEEK_CUR);
			units = getc(fp); // density units (0=no units)
			x_density = (getc(fp) << 8) + getc(fp);   // horizontal density
			y_density = (getc(fp) << 8) + getc(fp);   // vertical density
			if (units == 1)
			{
				x_dpi = x_density;
				y_dpi = y_density;
			}
			else if (units == 2)
			{
				x_dpi = x_density * 2.54;
				y_dpi = y_density * 2.54;
			}
			break;
		}
	}
	while (true)
	{
		// jump to the next FF
		while((marker = getc(fp)) != 0xFF);
		while((marker = getc(fp)) == 0xFF); 
		/*
		 * 0xFF 0xC2            // start of frame (SOF), SOF0 segement
		 * 0x00 0x11            // length of segment depends on the number of components (2 bytes)
		 * 0x08                 // bits per pixel (bpp, usually 8)
		 * 0x04 0x00            // image height
		 * 0x08 0x00            // image width
		 * 0x03                 // number of components (should be 1 or 3)
		 * 0x01 0x22 0x00       // 0x01=Y component, 0x22=sampling factor, quantization table number
		 * 0x02 0x11 0x01       // 0x02=Cb component, ...
		 * 0x03 0x11 0x01       // 0x03=Cr component, ...
		 */
		if (marker == 0xC0 || marker == 0xC2)
		{
			fseek(fp, 3, SEEK_CUR);
			height = (getc(fp) << 8) + getc(fp);   // height
			width = (getc(fp) << 8) + getc(fp);   // width
			break;
		}
	}
	fclose(fp);
	if (!width)
	{
		fprintf(stderr, "ERROR: failed to get size of image [%s]\n", fn);
		exit(1);
	}
	jpeg->width = width;
	jpeg->height = height;
	jpeg->x_dpi = x_dpi ? x_dpi : 96;
	jpeg->y_dpi = y_dpi ? y_dpi : 96;
}

void exif_II_dim(const char *fn, img_t *exif)
{
	uint16_t length;
	uint16_t marker;
	uint16_t width = 0, height = 0;
	double x_dpi = 96, y_dpi = 96;
	uint8_t units = 1;
	uint16_t x_density = 0, y_density = 0;
	uint32_t offset;

	FILE *fp = fopen(fn, "rb");
	if (!fp)
	{
		fprintf(stderr, "ERROR: failed opening [%s]\n", fn);
		exit(1);
	}
	// skip start of image (SOI)
	if (fseek(fp, 2, SEEK_SET))
	{
		fprintf(stderr, "ERROR: failed to seek [%s]\n", fn);
		exit(1);
	}
	/* xxd -c16 -g1 -u img.jpeg
	 * 0xFF, 0xD8,                     // SOI
	 * 0xFF, 0xE1,                     // APP1 segment
	 * 0x0F, 0xC5,                     // size of segment
	 * 0x45, 0x78, 0x69, 0x66,         // identifier string: "Exif"
	 * 0x00, 0x00,                     // ?
	 * 0x49, 0x49                      // byte order: 4D4D: MM; 4949: II ('II' = Little-endian (Intel, II) 'MM' = Big-endian (Motorola, MM)).
	 *                                    For example, value '305,419,896' is noted as 0x12345678 by sixteenth system. At the Motrola align,
	 *                                    it is stored as 0x12,0x34,0x56,0x78. If it's Intel align, it is stored as 0x78,0x56,0x34,0x12.
	 * 0x2A, 0x00,                     // Next 2bytes are always 2bytes-length value of 0x002A. If the data uses Intel align, next 2bytes
	 *                                    are "0x2a00". If it uses Motorola, they are "0x002a"
	 * 0x08, 0x00, 0x00, 0x00          // offset to the first IFD
	 * 0x0B, 0x00                      // No. of directory entry
	 * 0x0F, 0x01                      // XResolution Tag
	 * 0x1A, 0x01                      // XResolution
	 * 0x1B, 0x01                      // YResolution
	 * 0x28, 0x01                      // ResolutionUnit, '1' means no-unit, '2' means inch, '3' means centimeter.
	 * 0x02, 0xA0                      // ExifImageWidth
	 * 0x03, 0xA0                      // ExifImageHeight
	 */
	// get MM offset or II offset (TODO)
	while (!feof(fp))
	{
		while((marker = getc(fp)) != 0x49);
		offset = ftell(fp) + 1;
		break;
	}
	while (!feof(fp))
	{
		while((marker = getc(fp)) != 0x1A);
		while((marker = getc(fp)) == 0x1A);
		// 1A 01 05 00 01 00 00 00 B8 00 00 00
		// 1B 01 05 00 01 00 00 00 C0 00 00 00
		// 28 01 03 00 01 00 00 00 02 00 00 00
		if (marker == 0x01)
		{
			uint32_t x01 = ftell(fp);
			fseek(fp, 6, SEEK_CUR);
			offset += getc(fp) + (getc(fp) << 8);
			fseek(fp, offset, SEEK_SET);
			x_density = getc(fp) + (getc(fp) << 8);   // horizontal density
			fseek(fp, 10, SEEK_CUR);                   // jump to after 0x1B
			y_density = getc(fp) + (getc(fp) << 8);   // vertical density
			fseek(fp, x01 + 30, SEEK_SET);            // jump to after 0x28
			units = getc(fp) + (getc(fp) << 8);       // density units
			if (units == 1 || units == 2)
			{
				x_dpi = x_density;
				y_dpi = y_density;
			}
			else if (units == 3) // centimeter
			{
				x_dpi = x_density / 2.54;
				y_dpi = y_density / 2.54;
			}
			break;
		}
	}
	// get width and height of image
	rewind(fp);
	while (!feof(fp))
	{
		// jump to the next FF
		while((marker = getc(fp)) != 0x02);
		while((marker = getc(fp)) == 0x02); 
		// 02 A0 03 00 01 00 00 00 40 14 00 00
		// 03 A0 03 00 01 00 00 00 80 0D 00 00
		if (marker == 0xA0)
		{

			fseek(fp, 6, SEEK_CUR);
			width = getc(fp) + (getc(fp) << 8);
			fseek(fp, 10, SEEK_CUR);
			height = getc(fp) + (getc(fp) << 8);
			break;
		}
	}
	fclose(fp);
	if (!width)
	{
		fprintf(stderr, "ERROR: failed to get size of image [%s]\n", fn);
		exit(1);
	}
	exif->width = width;
	exif->height = height;
	exif->x_dpi = x_dpi ? x_dpi : 96;
	exif->y_dpi = y_dpi ? y_dpi : 96;
}

void exif_MM_dim(const char *fn, img_t *exif)
{
	uint16_t length;
	uint16_t marker;
	uint16_t width = 0, height = 0;
	double x_dpi = 96, y_dpi = 96;
	uint8_t units = 1;
	uint16_t x_density = 0, y_density = 0;
	uint32_t offset;

	FILE *fp = fopen(fn, "rb");
	if (!fp)
	{
		fprintf(stderr, "ERROR: failed opening [%s]\n", fn);
		exit(1);
	}
	// skip start of image (SOI)
	if (fseek(fp, 2, SEEK_SET))
	{
		fprintf(stderr, "ERROR: failed to seek [%s]\n", fn);
		exit(1);
	}
	/* xxd -c16 -g1 -u img.jpeg
	 * 0xFF, 0xD8,                     // SOI
	 * 0xFF, 0xE1,                     // APP1 segment
	 * 0x0F, 0xC5,                     // size of segment
	 * 0x45, 0x78, 0x69, 0x66,         // identifier string: "Exif"
	 * 0x00, 0x00,                     // ?
	 * 0x4D, 0x4D                      // byte order: 4D4D: MM; 4949: II (Little-endian or Big-endian),  For example, value '305,419,896' is noted as 0x12345678 by sixteenth system. At the Motrola align, it is stored as 0x12,0x34,0x56,0x78. If it's Intel align, it is stored as 0x78,0x56,0x34,0x12.
	 * 0x00, 0x2A,                     // Next 2bytes are always 2bytes-length value of 0x002A. If the data uses Intel align, next 2bytes are "0x2a00". If it uses Motorola, they are "0x002a"
	 * 0x00, 0x00, 0x00, 0x08          // offset to the first IFD
	 * 0x00, 0x0C                      // No. of directory entry
	 * 0x01, 0x00                      // XResolution Tag
	 * 0x01, 0x1A                      // XResolution
	 * 0x01, 0x1B                      // YResolution
	 * 0x01, 0x28                      // ResolutionUnit, '1' means no-unit, '2' means inch, '3' means centimeter.
	 * 0xA0, 0x02                      // ExifImageWidth
	 * 0xA0, 0x03                      // ExifImageHeight
	 */
	// get MM offset or II offset (TODO)
	while (!feof(fp))
	{
		while((marker = getc(fp)) != 0x4D);
		offset = ftell(fp) + 1;
		break;
	}
	while (!feof(fp))
	{
		while((marker = getc(fp)) != 0x01);
		while((marker = getc(fp)) == 0x01);
		// skip thumbnail
		// 01 1A 00 05 00 00 00 01 00 00 00 A4
		// 01 1B 00 05 00 00 00 01 00 00 00 AC
		// 01 28 00 03 00 00 00 01 00 02 00 00
		while((marker = getc(fp)) != 0x01);
		while((marker = getc(fp)) == 0x01);
		if (marker == 0x1A)
		{
			uint32_t x1A = ftell(fp);
			fseek(fp, 8, SEEK_CUR);
			offset += (getc(fp) << 8) + getc(fp);
			fseek(fp, offset, SEEK_SET);
			x_density = (getc(fp) << 8) + getc(fp);   // horizontal density
			fseek(fp, 6, SEEK_CUR);                   // jump to after 0x1B
			y_density = (getc(fp) << 8) + getc(fp);   // vertical density
			fseek(fp, x1A + 30, SEEK_SET);            // jump to after 0x28
			units = (getc(fp) << 8) + getc(fp);       // density units
			if (units == 1 || units == 2)
			{
				x_dpi = x_density;
				y_dpi = y_density;
			}
			else if (units == 3) // centimeter
			{
				x_dpi = x_density / 2.54;
				y_dpi = y_density / 2.54;
			}
			break;
		}
	}
	// get width and height of image
	rewind(fp);
	while (!feof(fp))
	{
		// jump to the next FF
		while((marker = getc(fp)) != 0xA0);
		while((marker = getc(fp)) == 0xA0); 
		// A0 02 00 04 00 00 00 01 00 00 08 00
		// A0 03 00 04 00 00 00 01 00 00 04 00
		if (marker == 0x02)
		{

			fseek(fp, 8, SEEK_CUR);
			width = (getc(fp) << 8) + getc(fp);
			fseek(fp, 10, SEEK_CUR);
			height = (getc(fp) << 8) + getc(fp);
			break;
		}
	}
	fclose(fp);
	if (!width)
	{
		fprintf(stderr, "ERROR: failed to get size of image [%s]\n", fn);
		exit(1);
	}
	exif->width = width;
	exif->height = height;
	exif->x_dpi = x_dpi ? x_dpi : 96;
	exif->y_dpi = y_dpi ? y_dpi : 96;
}

void bmp_dim(const char *fn, img_t *bmp)
{
	uint32_t width = 0, height = 0;
	double x_dpi = 96, y_dpi = 96;

	FILE *fp = fopen(fn, "rb");
	if (!fp)
	{
		fprintf(stderr, "ERROR: failed opening [%s]\n", fn);
		exit(1);
	}
	// Skip another 14 bytes to the start of the BMP height/width after the first 4 bytes.
	if (fseek(fp, 18, SEEK_SET))
	{
		fprintf(stderr, "ERROR: failed to seek [%s]\n", fn);
		exit(1);
	}
	if (fread(&width, sizeof(width), 1, fp) < 1)
		width = 0;
	if (fread(&height, sizeof(height), 1, fp) < 1)
		height = 0;
	if (!width)
	{
		fprintf(stderr, "ERROR: failed to get size of image [%s]\n", fn);
		exit(1);
	}
	bmp->width = width;
	bmp->height = height;
	bmp->x_dpi = x_dpi;
	bmp->y_dpi = y_dpi;
}

void gif_dim(const char *fn, img_t *gif)
{
	uint16_t width = 0, height = 0;
	double x_dpi = 96, y_dpi = 96;

	FILE *fp = fopen(fn, "rb");
	if (!fp)
	{
		fprintf(stderr, "ERROR: failed opening [%s]\n", fn);
		exit(1);
	}
	// Skip another 2 bytes to the start of the GIF height/width after the first 4 bytes.
	if (fseek(fp, 6, SEEK_SET))
	{
		fprintf(stderr, "ERROR: failed to seek [%s]\n", fn);
		exit(1);
	}
	if (fread(&width, sizeof(width), 1, fp) < 1)
		width = 0;
	if (fread(&height, sizeof(height), 1, fp) < 1)
		height = 0;
	if (!width)
	{
		fprintf(stderr, "ERROR: failed to get size of image [%s]\n", fn);
		exit(1);
	}
	height = ntohl(height);
	width = ntohl(width);

	gif->width = width;
	gif->height = height;
	gif->x_dpi = x_dpi;
	gif->y_dpi = y_dpi;
}

void img_scale(const float cw, const float ch, const bool kaspr, img_t *img)
{
	if (img->width && img->height && img->x_dpi && img->y_dpi)
	{
		img->x_scale = cw / img->width * img->x_dpi / 96.0;
		img->y_scale = ch / img->height * img->y_dpi / 96.0;
		if (kaspr)
		{
			if (img->x_scale < img->y_scale)
				img->y_scale = img->x_scale;
			else
				img->x_scale = img->y_scale;
		}
	}
}

int main(int argc, char *argv[])
{
	const char *fn = argv[1];
	img_t *img = calloc(1, sizeof(img_t));
	img->typ = img_typ(fn);
	switch (img->typ)
	{
		case 'p': puts("png"); png_dim(fn, img); break;
		case 'g': puts("gif"); gif_dim(fn, img); break;
		case 'b': puts("bmp"); bmp_dim(fn, img); break;
		case 'j': puts("jpeg"); jpeg_dim(fn, img); break;
		case 'i': puts("Exif(II)"); exif_II_dim(fn, img); break;
		case 'm': puts("Exif(MM)"); exif_MM_dim(fn, img); break;
		case 't': puts("tiff"); break;
		case 'x': puts("unknown"); break;
		default: puts("unknown");
	}
	printf("w%d; h%d; x:%f; y%f\n", img->width, img->height, img->x_dpi, img->y_dpi);
	free(img);
	return 0;
}
