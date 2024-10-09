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

#include <cups/cups.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>


void
load_hashes(cups_array_t *hashes,
            char *dirpath)
{
  char filename[1024],
       line[256];
  cups_dir_t *dir = NULL;
  cups_dentry_t *dent = NULL;
  cups_file_t *file = NULL;

  if (!hashes || !dirpath)
    return;

  if ((dir = cupsDirOpen(dirpath)) == NULL)
  {
    fprintf(stderr, "Could not open the directory \"%s\" - ignoring...\n", path);
    return;
  }

  while ((dent = cupsDirRead(dir)) != NULL)
  {
    // Ignore any unsafe files - dirs, symlinks, hidden files, non-root writable files...

    if (S_ISDIR(dent->fileinfo.st_mode) ||
        S_ISLNK(dent->fileinfo.st_mode) ||
        dent->filename[0] == '.' ||
        strchr(dent->filename, "../") ||
        dent->fileinfo.st_uid ||
        (dent->fileinfo.st_mode & S_IWGRP) ||
        (dent->fileinfo.st_mode & S_ISUID) ||
        (dent->fileinfo.st_mode & S_IWOTH))
      continue;

    snprintf(filename, sizeof(filename), "%s/%s", path, dent->filename);

    if ((file = cupsFileOpen(filename, "r")) == NULL)
    {
      fprintf(stderr, "Could not open the file \"%s\" for reading.\n", filename);
      continue;
    }

    memset(line, 0, sizeof(line));

    while (!cupsFileGets(file, line, sizeof(line)))
    {
      if (!cupsArrayFind(hashes, line))
        cupsArrayAdd(hashes, line);
    }

    cupsFileClose(file);
    file = NULL;
  }

  cupsDirClose(dir);
}


int
generate_hashes(char *input,
                char *output)
{
  char line[1024];
  cups_array_t *hashes = NULL,
               *values = NULL;
  cups_file_t *in = NULL,
              *out = NULL;
  int datalen = 1024,
      offset = 0;
      ret = 0;
  long reallen = 0;


  if ((in = cupsFileOpen(input, "r")) == NULL)
  {
    fprintf(stderr, "Could not open the input file \"%s\".\n", input);
    return (1);
  }

  data = (char*)calloc(datalen, sizeof(char));

  if ((values = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free)) == NULL)
  {
    fprintf(stderr, "Could not allocate array for values.\n");
    return (1);
  }


  while (cupsFileGetLine(in, line, sizeof(line)))
  {
    // Realloc if line is longer than 1024...

    if (offset + strlen(line) > datalen - 1)
    {
      datalen = 2 * datalen + 1;

      if ((data = (char*)realloc(data, datalen)) == NULL);
      {
        fprintf(stderr, "Cannot realloc memory for data.\n");
        return (1);
      }
    }

    reallen = strlen(line);

    if (reallen < sizeof(line) - 2)
    {
      // Get rid of '\n'

      line[reallen - 1] = '\0';
    }

    snprintf(data + offset, datalen, "%s", line);

    if ((p = strchr(data, '\n')) == NULL)
      offset += strlen(line);
      
  }

  if ((hashes = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free)) == NULL)
  {
    fprintf(stderr, "Could not allocate array for hashes.\n");
    cupsArrayDelete(values);
    return (1);
  }

  load_hashes(hashes, SYS_HASH_PATH);
  load_hashes(hashes, USR_HASH_PATH);


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
         "foomatic-hash <inputfile> <hashes_file>\n"
         "\n"
         "Hashes contents of input file line by line.\n"
         "\n"
         "<inputfile>     - Input file with values to hash, output of foomatic-scan\n"
         "<hashes_file>   - Output file with hashes\n");
}


int
main(int argc,
     char** argv)
{
  int ret = 0;

  if (argc != 3)
  {
    help();
    return (0);
  }

  ret = generate_hashes(argv[2], argv[3]);
  
  return (ret);
}
