/*
 *	Sherlock Library -- Mapping of Files
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 */

#include "lib/lib.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

void *
mmap_file(byte *name, unsigned *len, int writeable)
{
  int fd = open(name, writeable ? O_RDWR : O_RDONLY);
  struct stat st;
  void *x;

  if (fd < 0)
    return NULL;
  if (fstat(fd, &st) < 0)
    x = NULL;
  else
    {
      if (len)
	*len = st.st_size;
      if (st.st_size)
	{
	  x = mmap(NULL, st.st_size, writeable ? (PROT_READ | PROT_WRITE) : PROT_READ, MAP_SHARED, fd, 0);
	  if (x == MAP_FAILED)
	    x = NULL;
	}
      else	/* For empty file, we can return any non-zero address */
	return "";
    }
  close(fd);
  return x;
}

void
munmap_file(void *start, unsigned len)
{
  munmap(start, len);
}
