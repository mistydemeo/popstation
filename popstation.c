#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif
#include "iniparser.h"
#include <zlib.h>

#include "common.h"
#include "data.h"

#ifdef WIN32
#define snprintf _snprintf
#endif

#ifndef WIN32
typedef struct __attribute__((packed))
#else
#pragma pack(1)
typedef struct
#endif
{
  unsigned char adr : 4;
  unsigned char control : 4;
  unsigned char tno;
  unsigned char point;
  unsigned char amin;
  unsigned char asec;
  unsigned char aframe;
  unsigned char zero;
  unsigned char pmin;
  unsigned char psec;
  unsigned char pframe;
} tocentry;
#ifdef WIN32
#pragma pack()
#endif

// convert to non-packed binary-coded decimal
// https://en.wikipedia.org/wiki/Binary-coded_decimal#Basics
unsigned char bcd(unsigned char value) {
  unsigned int i;
  unsigned char result = 0;

  for (i = 0; value; i++) {
    result += (value % 10) * (int)pow(16, i);
    value = value / 10;
  }

  return result;
}

// wrapper for iniparser_getstring to let you specify a section and key
// separately
char *ini_get_string_from_section(dictionary *dict, const char *section,
                                  const char *key, char *def) {
  int key_length = strlen(section) + 1 + strlen(key) + 1;
  char *joined_key = (char *)malloc(key_length * sizeof(char));
  snprintf(joined_key, key_length, "%s:%s", section, key);
  return iniparser_getstring(dict, joined_key, def);
}

void *create_toc(char *iso_name, int *size) {
  int iso_name_length = strlen(iso_name);
  char *ccd_name = (char *)malloc((iso_name_length + 1) * sizeof(char));
  char entry_header[10];
  FILE *ccd_file;
  dictionary *ccd_dict;
  tocentry *entries;
  int count, i;

  strcpy(ccd_name, iso_name);
  ccd_name[iso_name_length - 3] = 'c';
  ccd_name[iso_name_length - 2] = 'c';
  ccd_name[iso_name_length - 1] = 'd';

  ccd_file = fopen(ccd_name, "rb");
  if (!ccd_file) {
    printf("No CCD file found. Assuming this is a pure-ISO9660 image!\n");
    return NULL;
  }
  fclose(ccd_file);

  printf("Making TOC from CCD file \"%s\"...\n", ccd_name);

  ccd_dict = iniparser_load(ccd_name);

  count = iniparser_getint(ccd_dict, "Disc:TocEntries", -1);

  if (count == -1) {
    printf("Failed to get TOC count from CCD, are you sure it's a valid CCD "
           "file?\r\n");

    return NULL;
  }

  entries = (tocentry *)malloc(sizeof(tocentry) * count);

  for (i = 0; i < count; i++) {
    snprintf(entry_header, 9, "Entry %d", i);

    entries[i].control =
        (unsigned char)(strtol(ini_get_string_from_section(
                                   ccd_dict, entry_header, "Control", "1"),
                               NULL, 0) &
                        0xF);

    entries[i].adr =
        (unsigned char)(strtol(ini_get_string_from_section(
                                   ccd_dict, entry_header, "ADR", "0"),
                               NULL, 0) &
                        0xF);

    entries[i].tno = (unsigned char)strtol(
        ini_get_string_from_section(ccd_dict, entry_header, "TrackNo", "0"),
        NULL, 0);

    entries[i].point = (unsigned char)strtol(
        ini_get_string_from_section(ccd_dict, entry_header, "Point", "0"), NULL,
        0);

    entries[i].amin = bcd((unsigned char)strtol(
        ini_get_string_from_section(ccd_dict, entry_header, "AMin", "0"), NULL,
        0));

    entries[i].asec = bcd((unsigned char)strtol(
        ini_get_string_from_section(ccd_dict, entry_header, "ASec", "0"), NULL,
        0));

    entries[i].aframe = bcd((unsigned char)strtol(
        ini_get_string_from_section(ccd_dict, entry_header, "AFrame", "0"),
        NULL, 0));

    entries[i].zero = (unsigned char)strtol(
        ini_get_string_from_section(ccd_dict, entry_header, "Zero", "0"), NULL,
        0);

    entries[i].pmin = bcd((unsigned char)strtol(
        ini_get_string_from_section(ccd_dict, entry_header, "PMin", "0"), NULL,
        0));

    entries[i].psec = bcd((unsigned char)strtol(
        ini_get_string_from_section(ccd_dict, entry_header, "PSec", "0"), NULL,
        0));

    entries[i].pframe = bcd((unsigned char)strtol(
        ini_get_string_from_section(ccd_dict, entry_header, "PFrame", "0"),
        NULL, 0));
  }

  iniparser_freedict(ccd_dict);

  *size = sizeof(tocentry) * count;

  return entries;
}

int toc = 0;
int toc_size;

void convert(char *input, char *output, char *title, char *code,
             int complevel) {
  FILE *in, *out, *base, *t;
  int i, offset, isosize, isorealsize, x;
  int index_offset, p1_offset, p2_offset, end_offset;
  IsoIndex *indexes;

  void *tocptr;

  in = fopen(input, "rb");
  if (!in) {
    ErrorExit("Cannot open %s\n", input);
  }

  isosize = getsize(in);
  isorealsize = isosize;

  if ((isosize % 0x9300) != 0) {
    isosize = isosize + (0x9300 - (isosize % 0x9300));
  }

  // printf("isosize, isorealsize %08X  %08X\n", isosize, isorealsize);

  base = fopen(BASE, "rb");
  if (!base) {
    ErrorExit("Cannot open %s\n", BASE);
  }

  out = fopen(output, "wb");
  if (!out) {
    ErrorExit("Cannot create %s\n", output);
  }

  printf("Writing header...\n");

  fread(base_header, 1, 0x28, base);

  if (base_header[0] != 0x50425000) {
    ErrorExit("%s is not a PBP file.\n", BASE);
  }

  sfo_size = base_header[3] - base_header[2];

  t = fopen("ICON0.PNG", "rb");
  if (t) {
    icon0_size = getsize(t);
    icon0 = 1;
    fclose(t);
  } else {
    icon0_size = base_header[4] - base_header[3];
  }

  t = fopen("ICON1.PMF", "rb");
  if (t) {
    icon1_size = getsize(t);
    icon1 = 1;
    fclose(t);
  } else {
    icon1_size = 0;
  }

  t = fopen("PIC0.PNG", "rb");
  if (t) {
    pic0_size = getsize(t);
    pic0 = 1;
    fclose(t);
  } else {
    pic0_size = 0; // base_header[6] - base_header[5];
  }

  t = fopen("PIC1.PNG", "rb");
  if (t) {
    pic1_size = getsize(t);
    pic1 = 1;
    fclose(t);
  } else {
    pic1_size = 0; // base_header[7] - base_header[6];
  }

  t = fopen("SND0.AT3", "rb");
  if (t) {
    snd_size = getsize(t);
    snd = 1;
    fclose(t);
  } else {
    snd = 0;
  }

  t = fopen("ISO.TOC", "rb");
  if (t) {
    toc_size = getsize(t);
    toc = 1;
    fclose(t);
  } else if ((tocptr = create_toc(input, &toc_size)) != NULL) {
    toc = 2;
  } else {
    toc = 0;
  }

  t = fopen("DATA.PSP", "rb");
  if (toc == 0 && t) {
    prx_size = getsize(t);
    prx = 1;
    fclose(t);
  } else {
    if (t) {
      fclose(t);
    }

    fseek(base, base_header[8], SEEK_SET);
    fread(psp_header, 1, 0x30, base);

    prx_size = psp_header[0x2C / 4];
  }

  int curoffs = 0x28;

  header[0] = 0x50425000;
  header[1] = 0x10000;

  header[2] = curoffs;

  curoffs += sfo_size;
  header[3] = curoffs;

  curoffs += icon0_size;
  header[4] = curoffs;

  curoffs += icon1_size;
  header[5] = curoffs;

  curoffs += pic0_size;
  header[6] = curoffs;

  curoffs += pic1_size;
  header[7] = curoffs;

  curoffs += snd_size;
  header[8] = curoffs;

  x = header[8] + prx_size;

  if ((x % 0x10000) != 0) {
    x = x + (0x10000 - (x % 0x10000));
  }

  header[9] = x;

  fwrite(header, 1, 0x28, out);

  printf("Writing sfo...\n");

  fseek(base, base_header[2], SEEK_SET);
  fread(buffer, 1, sfo_size, base);
  SetSFOTitle(buffer, title);
  strcpy(buffer + 0x108, code);
  fwrite(buffer, 1, sfo_size, out);

  printf("Writing icon0.png...\n");

  if (!icon0) {
    fseek(base, base_header[3], SEEK_SET);
    fread(buffer, 1, icon0_size, base);
    fwrite(buffer, 1, icon0_size, out);
  } else {
    t = fopen("ICON0.PNG", "rb");
    fread(buffer, 1, icon0_size, t);
    fwrite(buffer, 1, icon0_size, out);
    fclose(t);
  }

  if (icon1) {
    printf("Writing icon1.pmf...\n");

    t = fopen("ICON1.PMF", "rb");
    fread(buffer, 1, icon1_size, t);
    fwrite(buffer, 1, icon1_size, out);
    fclose(t);
  }

  if (!pic0) {
    // fseek(base, base_header[5], SEEK_SET);
    // fread(buffer, 1, pic0_size, base);
    // fwrite(buffer, 1, pic0_size, out);
  } else {
    printf("Writing pic0.png...\n");

    t = fopen("PIC0.PNG", "rb");
    fread(buffer, 1, pic0_size, t);
    fwrite(buffer, 1, pic0_size, out);
    fclose(t);
  }

  if (!pic1) {
    // fseek(base, base_header[6], SEEK_SET);
    // fread(buffer, 1, pic1_size, base);
    // fwrite(buffer, 1, pic1_size, out);
  } else {
    printf("Writing pic1.png...\n");

    t = fopen("PIC1.PNG", "rb");
    fread(buffer, 1, pic1_size, t);
    fwrite(buffer, 1, pic1_size, out);
    fclose(t);
  }

  if (snd) {
    printf("Writing snd0.at3...\n");

    t = fopen("SND0.AT3", "rb");
    fread(buffer, 1, snd_size, t);
    fwrite(buffer, 1, snd_size, out);
    fclose(t);
  }

  printf("Writing DATA.PSP...\n");

  if (prx) {
    t = fopen("DATA.PSP", "rb");
    fread(buffer, 1, prx_size, t);
    fwrite(buffer, 1, prx_size, out);
    fclose(t);
  } else {
    fseek(base, base_header[8], SEEK_SET);
    fread(buffer, 1, prx_size, base);
    fwrite(buffer, 1, prx_size, out);
  }

  offset = ftell(out);

  for (i = 0; i < header[9] - offset; i++) {
    fputc(0, out);
  }

  printf("Writing iso header...\n");

  fwrite("PSISOIMG0000", 1, 12, out);

  p1_offset = ftell(out);

  x = isosize + 0x100000;
  fwrite(&x, 1, 4, out);

  x = 0;
  for (i = 0; i < 0xFC; i++) {
    fwrite(&x, 1, 4, out);
  }

  memcpy(data1 + 1, code, 4);
  memcpy(data1 + 6, code + 4, 5);

  if (toc != 0) {
    printf("  Injecting TOC to iso header...\n");
    if (toc == 1) {
      t = fopen("ISO.TOC", "rb");
      fread(buffer, 1, toc_size, t);
      memcpy(data1 + 1024, buffer, toc_size);
      fclose(t);
    } else if (toc == 2) {
      memcpy(data1 + 1024, tocptr, toc_size);
      free(tocptr);
    }
  } else {
    printf("  Not injecting a TOC...\n");
  }

  fwrite(data1, 1, sizeof(data1), out);

  p2_offset = ftell(out);
  x = isosize + 0x100000 + 0x2d31;
  fwrite(&x, 1, 4, out);

  strcpy((char *)data2 + 8, title);
  fwrite(data2, 1, sizeof(data2), out);

  index_offset = ftell(out);

  printf("Writing indexes...\n");

  memset(dummy, 0, sizeof(dummy));

  offset = 0;

  if (complevel == 0) {
    x = 0x9300;
  } else {
    x = 0;
  }

  for (i = 0; i < isosize / 0x9300; i++) {
    fwrite(&offset, 1, 4, out);
    fwrite(&x, 1, 4, out);
    fwrite(dummy, 1, sizeof(dummy), out);

    if (complevel == 0)
      offset += 0x9300;
  }

  offset = ftell(out);

  for (i = 0; i < (header[9] + 0x100000) - offset; i++) {
    fputc(0, out);
  }

  printf("Writing iso...\n");

  if (complevel == 0) {
    while ((x = fread(buffer, 1, 1048576, in)) > 0) {
      fwrite(buffer, 1, x, out);
    }

    for (i = 0; i < (isosize - isorealsize); i++) {
      fputc(0, out);
    }
  } else {
    indexes = (IsoIndex *)malloc(sizeof(IsoIndex) * (isosize / 0x9300));

    if (!indexes) {
      fclose(in);
      fclose(out);
      fclose(base);

      ErrorExit("Cannot alloc memory for indexes!\n");
    }

    i = 0;
    offset = 0;

    while ((x = fread(buffer2, 1, 0x9300, in)) > 0) {
      if (x < 0x9300) {
        memset(buffer2 + x, 0, 0x9300 - x);
      }

      x = deflateCompress(buffer2, 0x9300, buffer, sizeof(buffer), complevel);

      if (x < 0) {
        fclose(in);
        fclose(out);
        fclose(base);
        free(indexes);

        ErrorExit("Error in compression!\n");
      }

      memset(&indexes[i], 0, sizeof(IsoIndex));

      indexes[i].offset = offset;

      if (x >= 0x9300) /* Block didn't compress */
      {
        indexes[i].length = 0x9300;
        fwrite(buffer2, 1, 0x9300, out);
        offset += 0x9300;
      } else {
        indexes[i].length = x;
        fwrite(buffer, 1, x, out);
        offset += x;
      }

      i++;
    }

    if (i != (isosize / 0x9300)) {
      fclose(in);
      fclose(out);
      fclose(base);
      free(indexes);

      ErrorExit("Some error happened.\n");
    }

    x = ftell(out);

    if ((x % 0x10) != 0) {
      end_offset = x + (0x10 - (x % 0x10));

      for (i = 0; i < (end_offset - x); i++) {
        fputc('0', out);
      }
    } else {
      end_offset = x;
    }

    end_offset -= header[9];
  }

  printf("Writing special data...\n");

  fseek(base, base_header[9] + 12, SEEK_SET);
  fread(&x, 1, 4, base);

  x += 0x50000;

  fseek(base, x, SEEK_SET);
  fread(buffer, 1, 8, base);

  if (memcmp(buffer, "STARTDAT", 8) != 0) {
    ErrorExit("Cannot find STARTDAT in %s.\n", "Not a valid PSX eboot.pbp\n",
              BASE);
  }

  fseek(base, x, SEEK_SET);

  while ((x = fread(buffer, 1, 1048576, base)) > 0) {
    fwrite(buffer, 1, x, out);
  }

  if (complevel != 0) {
    printf("Updating compressed indexes...\n");

    fseek(out, p1_offset, SEEK_SET);
    fwrite(&end_offset, 1, 4, out);

    end_offset += 0x2d31;
    fseek(out, p2_offset, SEEK_SET);
    fwrite(&end_offset, 1, 4, out);

    fseek(out, index_offset, SEEK_SET);
    fwrite(indexes, 1, sizeof(IsoIndex) * (isosize / 0x9300), out);
  }

  fclose(in);
  fclose(out);
  fclose(base);
}

void usage(char *prog) {
  ErrorExit("Usage: %s title gamecode compressionlevel file.bin\n",
            prog);
}

int main(int argc, char *argv[]) {
  int i;

  if (argc != 5) {
    printf("Invalid number of arguments.\n");
    usage(argv[0]);
  }

  if (strlen(argv[2]) != 9) {
    printf("Invalid game code.\n");
    usage(argv[0]);
  }

  for (i = 0; i < N_GAME_CODES; i++) {
    if (strncmp(argv[2], gamecodes[i], 4) == 0)
      break;
  }

  if (i == N_GAME_CODES) {
    printf("Invalid game code.\n");
    usage(argv[0]);
  }

  for (i = 4; i < 9; i++) {
    if (argv[2][i] < '0' || argv[2][i] > '9') {
      printf("Invalid game code.\n");
      usage(argv[0]);
    }
  }

  if (strlen(argv[3]) != 1) {
    printf("Invalid compression level.\n");
    usage(argv[0]);
  }

  if (argv[3][0] < '0' || argv[3][0] > '9') {
    printf("Invalid compression level.\n");
    usage(argv[0]);
  }

  convert(argv[4], "EBOOT.PBP", argv[1], argv[2], argv[3][0] - '0');

  printf("Done.\n");
  return 0;
}
