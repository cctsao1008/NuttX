/****************************************************************************
 * libc/stdio/lib_gets_s.c
 *
 *   Copyright (C) 2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdio.h>
#include <limits.h>
#include <string.h>

/****************************************************************************
 * Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Global Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Global Constant Data
 ****************************************************************************/

/****************************************************************************
 * Global Variables
 ****************************************************************************/

/****************************************************************************
 * Private Constant Data
 ****************************************************************************/

/****************************************************************************
 * Private Variables
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gets
 *
 * Description:
 *   gets() reads a line from stdin into the buffer pointed to by s until
 *   either a terminating newline or EOF, which it replaces with '\0'.  Reads
 *   at most n-1 characters from stdin into the array pointed to by str until
 *   new-line character, end-of-file condition, or read error. A null
 *   character is written immediately after the last character read into the
 *   array, or to str[0] if no characters were read.
 *
 *   If n is zero or is greater than RSIZE_MAX, a null character is written
 *   to str[0] but the function reads and discards characters from stdin
 *   until new-line character, end-of-file condition, or read error (not
 *   implemented).
 *
 *   If n-1 characters have been read, continues reading and discarding the
 *   characters from stdin until new-line character, end-of-file condition,
 *   or read error (not implemented).
 *
 **************************************************************************/

FAR char *gets_s(FAR char *s, rsize_t n)
{
  /* gets is equivalent to fgets using stdin.  So let fgets do most of the
   * work.
   */

  FAR char *ret = fgets(s, n, stdin);
  if (ret)
    {
      /* A subtle difference from fgets is that gets_s replaces end-of-line
       * markers with null terminators.  We will do that as a second step
       * (with some loss in performance).
       */

      int len = strlen(ret);
      if (len > 0 && ret[len-1] == '\n')
        {
           ret[len-1] = '\0';
        }
    }

  return ret;
}