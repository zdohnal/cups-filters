//
// foomaticrip.c
//
// Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
// Copyright (C) 2008 Lars Karlitski (formerly Uebernickel) <lars@karlitski.net>
//
// This file is part of foomatic-rip.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <ctype.h>
#include <cups/cups.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <ppd/ppd.h>


#if CUPS_VERSION_MAJOR <= 2 && CUPS_VERSION_MINOR < 5
#  define cupsArrayGetFirst(ar) cupsArrayFirst(ar)
#  define cupsArrayGetNext(ar) cupsArrayNext(ar)
#endif


cups_array_t *data = NULL;


typedef struct dstr
{
  char *data;
  size_t len;
  size_t alloc;
} dstr_t;


int
startswith(const char *str,
	   const char *prefix)
{
  return (str ? (strncmp(str, prefix, strlen(prefix)) == 0) : 0);
}


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
print_file(char         *filename,
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
free_dstr(dstr_t *ds)
{
  free(ds->data);
  free(ds);
}


dstr_t *
create_dstr()
{
  dstr_t *ds = malloc(sizeof(dstr_t));
  ds->len = 0;
  ds->alloc = 32;
  ds->data = malloc(ds->alloc);
  ds->data[0] = '\0';
  return (ds);
}


void
dstrassure(dstr_t *ds,
	   size_t alloc)
{
  if (ds->alloc < alloc)
  {
    ds->alloc = alloc;
    ds->data = realloc(ds->data, ds->alloc);
  }
}


void
dstrclear(dstr_t *ds)
{
  ds->len = 0;
  ds->data[0] = '\0';
}


int
dstrendswith(dstr_t *ds,
	     const char *str)
{
  int len = strlen(str);
  char *pstr;

  if (ds->len < len)
    return (0);
  pstr = &ds->data[ds->len - len];
  return (strcmp(pstr, str) == 0);
}


void
dstrcat(dstr_t *ds,
	const char *src)
{
  size_t srclen = strlen(src);
  size_t newlen = ds->len + srclen;

  if (newlen >= ds->alloc)
  {
    do
    {
      ds->alloc *= 2;
    }
    while (newlen >= ds->alloc);
    ds->data = realloc(ds->data, ds->alloc);
  }

  memcpy(&ds->data[ds->len], src, srclen +1);
  ds->len = newlen;
}


void
dstrremovenewline(dstr_t *ds)
{
  if (!ds->len)
    return;

  if (ds->data[ds->len -1] == '\r' || ds->data[ds->len -1] == '\n')
  {
    ds->data[ds->len -1] = '\0';
    ds->len -= 1;
  }

  if (ds->len < 2)
    return;

  if (ds->data[ds->len -2] == '\r')
  {
    ds->data[ds->len -2] = '\0';
    ds->len -= 2;
  }
}


void
dstrtrim_right(dstr_t *ds)
{
  if (!ds->len)
    return;

  while (isspace(ds->data[ds->len - 1]))
    ds->len -= 1;
  ds->data[ds->len] = '\0';
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
