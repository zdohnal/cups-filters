//
// foomatic-scan.c
//
// Copyright (C) 2024-2025 Zdenek Dohnal <zdohnal@redhat.com>
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "util.h"
#include <ctype.h>
#include <cups/cups.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <ppd/ppd.h>


cups_array_t *data = NULL;


cups_array_t *
generate_array(char *filename)
{
  char          line[2048];
  cups_array_t *ar = NULL;
  cups_file_t  *fp = NULL;

  memset(line, 0, sizeof(line));

  if((ar = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free)) == NULL)
  {
    fprintf(stderr, "Cannot allocate array.\n");
    return (NULL);
  }

  if (!access(filename, F_OK))
  {
    fprintf(stderr, "No such file exists, try creating a new later.\n");
    return (ar);
  }

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    fprintf(stderr, "Cannot open file \"%s\" for read.\n", filename);
    cupsArrayDelete(ar);
    return (NULL);
  }

  while (cupsFileGets(fp, line, sizeof(line)))
  {
    cupsArrayAdd(ar, line);

    memset(line, 0, sizeof(line));
  }

  cupsFileClose(fp);

  return (ar);
}


void
write_file(char         *filename,
           cups_array_t *ar)
{
  cups_file_t *f = NULL;

  if ((f = cupsFileOpen(filename, "w")) == NULL)
  {
    fprintf(stderr, "Cannot open \"%s\" for write.\n", filename);
    return;
  }

  for (char *s = (char*)cupsArrayGetFirst(ar); s; s = (char*)cupsArrayGetNext(ar))
    cupsFilePrintf(f, "%s\n", s);

  cupsFileClose(f);
}


void
read_ppd(cups_file_t *file)
{
  char line[256];            // PPD line length is max 255 (excl. \0)
  char *p;
  char key[128], name[64], text[64];
  unsigned char hash[64];


  dstr_t *value = create_dstr(); // value can span multiple lines

  dstrassure(value, 256);

  while (!cupsFileEOF(file))
  {
    cupsFileGets(file, line, 256);

    if (line[0] != '*' || startswith(line, "*%"))
      continue;

    // get the key
    if (!(p = strchr(line, ':')))
      continue;
    *p = '\0';

    key[0] = name[0] = text[0] = '\0';
    sscanf(line, "*%127s%*[ \t]%63[^ \t/=)]%*1[/=]%63[^\n]", key, name, text);

    // get the value
    dstrclear(value);
    sscanf(p +1, " %255[^\r\n]", value->data);
    value->len = strlen(value->data);
    if (!value->len)
      fprintf(stderr, "PPD: Missing value for key \"%s\"\n", line);

    while (1)
    {
      // "&&" is the continue-on-next-line marker
      if (dstrendswith(value, "&&"))
      {
	value->len -= 2;
	value->data[value->len] = '\0';
      }
      // quoted but quotes are not yet closed
      else if (value->data[0] == '\"' && !strchr(value->data +1, '\"'))
	dstrcat(value, "\n"); // keep newlines in quoted string
      // not quoted, or quotes already closed
      else
	break;

      cupsFileGets(file, line, 256);
      dstrcat(value, line);
      dstrremovenewline(value);
    }

    memset(hash, 0, sizeof(hash));

    // remove quotes
    if (value->data[0] == '\"')
    {
      memmove(value->data, value->data +1, value->len +1);
      p = strrchr(value->data, '\"');
      if (!p)
      {
	fprintf(stderr, "Invalid line: \"%s: ...\"\n", key);
	continue;
      }
      *p = '\0';
    }
    // remove last newline
    dstrremovenewline(value);

    // remove last whitespace
    dstrtrim_right(value);

    if (!value->data || !value->data[0])
      continue;

    if (!strcmp(key, "FoomaticRIPCommandLine") || !strcmp(key, "FoomaticRIPCommandLinePDF") || !strcmp(key, "FoomaticRIPOptionSetting"))
    {
      if (!cupsArrayFind(data, value->data))
        cupsArrayAdd(data, value->data);
    }
  }

  free_dstr(value);
}


int
get_values_from_ppd(char *filename)
{
  cups_file_t *file = NULL;
  int ret = 0;

  if ((file = cupsFileOpen(filename, "r")) == NULL)
  {
    fprintf(stderr, "Cannot open \"%s\" for reading.\n", filename);
    return (1);
  }

  read_ppd(file);

  cupsFileClose(file);

  return (ret);
}

ppd_collection_t *
copy_col(char *path)
{
  ppd_collection_t *col = NULL;

  if ((col = (ppd_collection_t*)calloc(1, sizeof(ppd_collection_t))) == NULL)
  {
    fprintf(stderr, "Cannot allocate memory for PPD collection.\n");
    return (NULL);
  }

  if ((col->path = (char*)calloc(strlen(path) + 1, sizeof(char))) == NULL)
  {
    fprintf(stderr, "Cannot allocate memory for PPD path.\n");
    free(col);
    return (NULL);
  }

  snprintf(col->path, strlen(path) + 1, "%s", path);

  return (col);
}


void
free_col(ppd_collection_t *col)
{
  free(col->path);
  free(col);
}


int
compare_col(ppd_collection_t *a, ppd_collection_t *b)
{
  if(!strcmp(a->path, b->path))
    return (0);

  return (1);
}


int
get_values_from_ppdpaths(char *ppdpaths)
{
  char *path = NULL,
       *start = NULL,
       *end = NULL;
  cups_array_t *ppd_collections = NULL,
               *ppds = NULL;
  cups_file_t *ppdfile = NULL;
  int ret = 0;
  ppd_info_t *ppd = NULL;


  if ((ppd_collections = cupsArrayNew3((cups_array_func_t)compare_col, NULL, NULL, 0, (cups_acopy_func_t)copy_col, (cups_afree_func_t)free_col)) == NULL)
  {
    fprintf(stderr, "Could not allocate PPD collection array.\n");
    return (1);
  }

  if ((path = strchr(ppdpaths, ',')) == NULL)
    cupsArrayAdd(ppd_collections, ppdpaths);
  else
  {
    for (start = end = ppdpaths; *end; start = end)
    {
      if ((end = strchr(start, ',')) != NULL)
        *end++ = '\0';
      else
        end = start + strlen(start);

      if (!cupsArrayFind(ppd_collections, start))
        cupsArrayAdd(ppd_collections, start);
    }
  }

  if ((ppds = ppdCollectionListPPDs(ppd_collections, 0, 0, NULL, NULL, NULL)) == NULL)
  {
    fprintf(stdout, "No PPDs found, exiting.\n");
    goto end;
  }

  for (ppd = (ppd_info_t*)cupsArrayGetFirst(ppds); ppd; ppd = (ppd_info_t*)cupsArrayGetNext(ppds))
  {
    if ((ppdfile = ppdCollectionGetPPD(ppd->record.name, ppd_collections, NULL, NULL)) == NULL)
      continue;

    read_ppd(ppdfile);

    cupsFileClose(ppdfile);
  }


end:
  for (ppd = (ppd_info_t*)cupsArrayGetFirst(ppds); ppd; ppd = (ppd_info_t*)cupsArrayGetNext(ppds))
    free(ppd);

  cupsArrayDelete(ppds);

  cupsArrayDelete(ppd_collections);

  return (ret);
}


void
help()
{
  printf("Usage:\n"
         "foomatic-scan --ppd <ppdfile> <file>\n"
         "foomatic-scan --ppd-paths <path1,path2...pathN> <file>\n"
         "\n"
         "Finds values of FoomaticRIPCommandLine, FoomaticRIPPDFCommandLine\n"
         "and FoomaticRIPOptionSetting from the specified PPDs and appends them\n"
         "into the specified file.\n"
         "\n"
         "--ppd <ppdfile>                   - PPD file to read\n"
         "--ppd-paths <path1,path2...pathN> - Paths to look for PPDs, available only with libppd\n");
}


int
main(int argc,
     char** argv)
{
  if (argc != 4)
  {
    help();
    return (0);
  }
  
  data = generate_array(argv[3]);

  if (!strcmp(argv[1], "--ppd"))
  {
    if (get_values_from_ppd(argv[2]))
      return (1);
  }
  else if (!strcmp(argv[1], "--ppd-paths"))
  {
    if (get_values_from_ppdpaths(argv[2]))
      return (1);
  }
  else
  {
    fprintf(stderr, "Unsupported argument.\n");
    return (1);
  }

  print_file(argv[3], data);

  cupsArrayDelete(data);

  return (0);
}
