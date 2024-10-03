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


cups_array_t *hashes = NULL;
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

  for (char *s = (char*)cupsArrayFirst(ar); s; s = (char*)cupsArrayNext(ar))
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
read_ppd_file(FILE *file)
{
  char line[256];            // PPD line length is max 255 (excl. \0)
  char *p;
  char key[128], name[64], text[64];
  unsigned char hash[64];
  char hash_string[129];
  dstr_t *value = create_dstr(); // value can span multiple lines

  memset(hash_string, 0, sizeof(hash_string));

  dstrassure(value, 256);

  while (!feof(file))
  {
    fgets(line, 256, file);

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

      fgets(line, 256, file);
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

    if (strcmp(key, "FoomaticRIPCommandLine") == 0)
    {
      if ((cupsHashData("sha2-256", value->data, strlen(value->data), hash, sizeof(hash))) == -1)
      {
        fprintf(stderr, "\"%s\" - Error when hashing\n", value->data);
        continue;
      }

      if ((cupsHashString(hash, sizeof(hash), hash_string, sizeof(hash_string))) == NULL)
      {
        fprintf(stderr, "Error when encoding hash to hexadecimal\n");
        continue;
      }
    }
    else if (strcmp(key, "FoomaticRIPCommandLinePDF") == 0)
    {
      if ((cupsHashData("sha2-256", value->data, strlen(value->data), hash, sizeof(hash))) == -1)
      {
        fprintf(stderr, "\"%s\" - Error when hashing\n", value->data);
        continue;
      }

      if ((cupsHashString(hash, sizeof(hash), hash_string, sizeof(hash_string))) == NULL)
      {
        fprintf(stderr, "Error when encoding hash to hexadecimal\n");
        continue;
      }
    }
    else if (!strcmp(key, "FoomaticRIPOptionSetting"))
    {
      if ((cupsHashData("sha2-256", value->data, strlen(value->data), hash, sizeof(hash))) == -1)
      {
        fprintf(stderr, "\"%s\" - Error when hashing\n", value->data);
        continue;
      }

      if ((cupsHashString(hash, sizeof(hash), hash_string, sizeof(hash_string))) == NULL)
      {
        fprintf(stderr, "Error when encoding hash to hexadecimal\n");
        continue;
      }
    }

    if (*hash_string)
    {
      if (!cupsArrayFind(hashes, hash_string))
        cupsArrayAdd(hashes, hash_string);

      if (data && !cupsArrayFind(data, value->data))
        cupsArrayAdd(data, value->data);

      memset(hash_string, 0, sizeof(hash_string));
    }
  }

  free_dstr(value);
}


void
help()
{
  printf("Usage:\n"
         "foomatic-hash --ppd <ppdfile> [--data <datafile>] <hashes_file>\n"
         "\n"
         "Hashes values of FoomaticRIPCommandLine, FoomaticRIPPDFCommandLine\n"
         "and FoomaticRIPOptionSetting from the specified PPD and appends them\n"
         "into the specified file.\n"
         "\n"
         "--ppd <ppdfile>     - PPD file to read\n"
         "-d                  - debugging mode - prints the original value for review\n");
}


int
main(int argc,
     char** argv)
{
  char *hashes_filename = NULL,
       *ppdname = NULL,
       *dataname = NULL;
  FILE *ppd = NULL;
  int want_data = 0;

  if (argc < 4 || argc > 6)
  {
    help();
    return (0);
  }
  
  for (int i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "--ppd"))
    {
      if (i + 1 == argc)
        break;

      ppdname = argv[++i];
    }
    else if (!strcmp(argv[i], "--data"))
    {
      want_data = 1;

      if (i + 1 == argc)
        break;

      dataname = argv[++i];
    }
    else
    {
      hashes_filename = argv[i];
    }
  }

  if (!ppdname || !hashes_filename || (want_data && !dataname))
  {
    fprintf(stderr, "Missing required arguments.\n");
    help();
    return (1);
  }

  if ((ppd = fopen(ppdname, "r")) == NULL)
  {
    fprintf(stderr, "Cannot open PPD file \"%s\"", ppdname);
    return (1);
  }

  if ((hashes = generate_array(hashes_filename)) == NULL)
    return (1);

  if (want_data && (data = generate_array(hashes_filename)) == NULL)
    return (1);

  read_ppd_file(ppd);

  fclose(ppd);

  if (want_data)
    print_file(dataname, data);

  print_file(hashes_filename, hashes);

  cupsArrayDelete(hashes);

  if (want_data)
    cupsArrayDelete(data);

  return (0);
}
