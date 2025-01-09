//
// foomatic-hash.c
//
// Copyright (C) 2024-2025 Zdenek Dohnal <zdohnal@redhat.com>
//
// This file converts output of foomatic-scan - values of FoomaticRIPCommandLine,
// FoomaticRIPCommandLinePDF and FoomaticRIPOptionSetting - into hashes.
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
  char          filename[1024],
                line[256];
  cups_dir_t    *dir = NULL;
  cups_dentry_t *dent = NULL;
  cups_file_t   *file = NULL;

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


cups_array_t *
load_local_hashes()
{
  cups_array_t *hashes = NULL;


  if ((hashes = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free)) == NULL)
  {
    fprintf(stderr, "Could not allocate array for existing hashes.\n");
    return (NULL);
  }

  load_hashes(hashes, SYS_HASH_PATH);
  load_hashes(hashes, USR_HASH_PATH);

  return (hashes);
}


int
write_hashes(cups_array_t *hashes,
             char         *output)
{
  char        *data = NULL;
  cups_file_t *out = NULL;

  if ((out = cupsFileOpen(output, "w")) == NULL)
  {
    fprintf(stderr, "The file \"%s\" cannot be opened for write.\n", output);

    return (1);
  }

  for (data = (char*)cupsArrayGetFirst(hashes); data; data = (char*)cupsArrayGetNext(hashes))
    cupsFilePrintf(out, "%s\n", data);

  cupsFileClose(out);

  return (0);
}


cups_array_t *
get_values(char *input)
{
  char         line[1024];
  char         *data = NULL;
  char         *newdata = NULL;
  cups_array_t *values = NULL;
  cups_file_t  *in = NULL;
  int          datalen = 1024,
               offset = 0;
  size_t       reallen = 0;


  memset(line, 0, sizeof(line));

  if ((in = cupsFileOpen(input, "r")) == NULL)
  {
    fprintf(stderr, "Could not open the input file \"%s\".\n", input);
    return (NULL);
  }

  if ((data = (char*)calloc(datalen, sizeof(char))) == NULL)
  {
    fprintf(stderr, "Could not allocate data array.\n");

    goto fail;
  }

  if ((values = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free)) == NULL)
  {
    fprintf(stderr, "Could not allocate array for values.\n");

    goto fail;
  }


  while (cupsFileGetLine(in, line, sizeof(line)))
  {
    reallen = strlen(line);

    // Realloc if line is longer than 1023...

    if (offset + reallen > datalen - 1)
    {
      datalen = 2 * datalen + 1;

      if ((newdata = (char*)realloc(data, datalen)) == NULL);
      {
        fprintf(stderr, "Cannot realloc memory for data.\n");

        goto fail;
      }
      else
        data = newdata;
    }

    snprintf(data + offset, datalen - offset, "%s", line);

    memset(line, 0, sizeof(line));

    if (data[offset + reallen - 1] != '\n')
    {
      // Used only if the current line is of 1024+ characters - read until we have the whole line

      offset += reallen;
      continue;
    }
    else
    {
      // Get rid of '\n'

      data[offset + reallen - 1] = '\0';

      if (!cupsArrayFind(values, data))
        cupsArrayAdd(values, data);

      offset = 0;
      memset(data, 0, datalen);
    }
  }

  return (values);


fail:
  if (values)
    cupsArrayDelete(values);

  if (data)
    free(data);

  if (in)
    cupsFileClose(in);

  return (NULL);
}


int
generate_hash_file(char *input,
                   char *output)
{
  cups_array_t  *allhashes = NULL,
                *hashes = NULL
                *values = NULL;
  int           ret = 0;
  unsigned char hash[32];
  unsigned char hash_string[65];


  if ((values = get_values(input)) == NULL)
    return (1);

  if ((allhashes = load_local_hashes()) == NULL)
  {
    ret = 1;

    goto fail;
  }

  if ((hashes = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free)) == NULL)
  {
    fprintf(stderr, "Could not allocate array for hashes.\n");

    ret = 1;

    goto fail;
  }

  for (data = (char*)cupsArrayGetFirst(values); data; data = (char*)cupsArrayGetNext(values))
  {
    if ((cupsHashData("sha2-256", data, strlen(data), hash, sizeof(hash))) == -1)
    {
      fprintf(stderr, "\"%s\" - Error when hashing\n", data);
      continue;
    }

    if ((cupsHashString(hash, sizeof(hash), hash_string, sizeof(hash_string))) == NULL)
    {
      fprintf(stderr, "Error when encoding hash to hexadecimal\n");
      continue;
    }

    if (!cupsArrayFind(allhashes, hash_string))
      cupsArrayAdd(hashes, hash_string);
  }

  ret = write_hashes(hashes, output);

fail:
  if (values)
    cupsArrayDelete(values);

  if (allhashes)
    cupsArrayDelete(allhashes);

  if (hashes)
    cupsArrayDelete(hashes);

  return (ret);
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
  int ret;

  if (argc != 3)
  {
    help();
    return (0);
  }

  ret = generate_hash_file(argv[2], argv[3]);
  
  return (ret);
}
