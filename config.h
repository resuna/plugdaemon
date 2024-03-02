/*
 * PLUGDAEMON. Copyright (c) 2004 Peter da Silva. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the program and author may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define MAX_PROXIES 32
#define MAX_CLIENTS 16384 /* These are held for a whole timeout period */
#define USAGE_FACTOR 4 /* expected # proxies per client */
#define IO_SIZE 2048	/* size of reads and writes */
#define GRAVESITES 1024	/* How many dead procs to expect in the main loop */
#ifdef BIGHASH
#define HASH_SIZE 661	/* Largest prime below 666 */
#else
#define HASH_SIZE 41	/* Largest prime below 42 */
#endif
#define hash(n) ((n) % HASH_SIZE)	/* Shupid hash algorithm */

/* Random OS-Specific stuff */
#define SA2ASCII_BUFSIZ 78 /* big enough for IPV6 addr + : + port */

#ifdef sa_sigaction
# define SA_HANDLER sa_sigaction
#else
# define SA_HANDLER sa_handler
#endif

#ifdef __OpenBSD__ 
# define SA_HANDLER_ARG2_T siginfo_t *
#endif

#ifdef __FreeBSD__
# if __FreeBSD__ < 4
#  define SA_HANDLER_ARG2_T int
# else
#  define SA_HANDLER_ARG2_T siginfo_t *
# endif
#endif

/* Linux changes as per Anthony de Boer (ADB) */
#ifdef __linux__
# ifdef sa_sigaction
#  define SA_HANDLER_ARG2_T siginfo_t *
# else
#  define WAITER_ALTDEF
# endif
  /* the symbol __GLIBC_PREREQ seems to have been added at the same time as
   * in_addr_t + dietlibc changes by al
   */
# if (!(defined(__GLIBC_PREREQ) || defined(__dietlibc__)))
   typedef unsigned long int in_addr_t;
# endif
#endif

/* dietlibc changes by al */
#if defined(__dietlibc__)
  typedef uint32_t u_long;
  typedef unsigned short u_short;
#endif


/* Mac OS X 10.1.5 */
#ifdef __APPLE__
# include <AvailabilityMacros.h>
# ifndef MAC_OS_X_VERSION_10_2
   typedef u_int32_t in_addr_t;
# else
#  define SA_HANDLER_ARG2_T siginfo_t *
# endif
#endif

#if defined(__osf__) && defined(__alpha)
# define SA_HANDLER_ARG2_T struct siginfo *
#endif

#ifndef SA_HANDLER_ARG2_T
# define SA_HANDLER_ARG2_T int
#endif                         

