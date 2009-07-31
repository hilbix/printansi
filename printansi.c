/* $Header$
 *
 * ANSI escape output.
 *
 * This Works is placed under the terms of the Copyright Less License,
 * see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.
 *
 * Note that the required tinolib IS NOT CLL, except where noted.
 *
 * $Log$
 * Revision 1.1  2009-07-31 22:26:28  tino
 * First version
 *
 */

#include "tino/alarm.h"
#include "tino/put.h"
#include "tino/getopt.h"
#include "tino/buf_line.h"
#include "tino/fileerr.h"
#include "tino/sleep.h"

#include "printansi_version.h"

static int	flag_loop, flag_zero, flag_verbose;
static char	*prefix_string, *suffix_string, *argument_separator, *quote_string;
static char	*file_name;
static int	cycle_time;

static int	out=-1, quotes;

static int	within_line;

static int	arm, tick, opentime;

static void
verbose(const char *s, ...)
{
  tino_va_list	list;

  if (!flag_verbose)
    return;
  tino_va_start(list, s);
  tino_vfprintf(stderr, &list);
  tino_va_end(list);
  tino_fprintf(stderr, "\n");
}

static void
flushnclose(void)
{
  if (out<0)
    return;

  tino_io_write(out, NULL, 1);
  tino_file_closeA(out, file_name);
  verbose("closed file %s", file_name);
  out	= -1;

  tino_relax();
}

static int
alarm_cb(void *user, long delta, time_t now, long run)
{
  opentime++;
  if (file_name && out>=0 && arm)
    switch (++tick)
      {
      case 2:
	tino_io_write(out, NULL, 1);
	break;
      case 3:
	flushnclose();
	break;
      }
  return 0;
}

static void
output_open(void)
{
  if (out>=0)
    return;

  if (!file_name)
    out	= tino_io_fd(1);
  else
    {
      tino_alarm_set(1, alarm_cb, NULL);
      for (;;)
	{
	  int	fd;

	  if ((fd=tino_file_open_createE(file_name, O_RDWR|O_APPEND, 0600))<0)
	    tino_exit("cannot open %s", file_name);

          verbose("try to lock file %s", file_name);
	  TINO_ALARM_RUN();

	  /* Lock file
	   */
	  if (!tino_file_lockI(fd, 1, 1))
	    {
	      out	= tino_io_fd(fd);
              verbose("appending to %s", file_name);
	      opentime	= 0;
	      break;
	    }
	  tino_file_closeE(fd);
	}
    }
}

static void
outc(char c)
{
  tino_io_put(out, c);
  within_line	= 1;
}

static void
outs(const char *s)
{
  tino_io_write(out, s, strlen(s));
  within_line	= 1;
}

static void
std(const char *s, char def)
{
  if (!s)
    outc(def);
  else if (!*s)
    outc(0);
  else
    outs(s);
}

static void
sep(void)
{
  if (within_line)
    std(argument_separator, ' ');    
}

static void
prefix(void)
{
  if (within_line)
    sep();
  else
    {
      output_open();
      if (prefix_string)
	outs(prefix_string);
    }
}

static void
newline(void)
{
  if (within_line)
    std(suffix_string, '\n');
  within_line	= 0;
  quotes	= 0;
}

static void
quote(void)
{
  if (!quote_string)
    return;
  if (!*quote_string)
    {
      outc(0);
      return;
    }
  if (!quote_string[quotes])
    quotes	= 0;
  outc(quote_string[quotes++]);
}

static void
printansi(const char *s)
{
  prefix();
  quote();
  tino_put_ansi(out, s, NULL);
  quote();
  within_line	= 1;
}

/* In future this will use io.h too, of course, as soon as input is
 * implemented.
 */
static void
printfile(int fd)
{
  TINO_BUF	 buf;
  const char	*s;

  tino_buf_initO(&buf);
  while ((s=tino_buf_line_read(&buf, fd, flag_zero ? 0 : '\n'))!=0)
    {
      arm	= 0;
      printansi(s);
      newline();
      arm	= 1;
      tick	= 0;
      if (cycle_time && opentime>cycle_time)
	flushnclose();
    }
}

static void
printfiles(int argc, char **argv, int argn)
{
  for (; argn<argc; argn++)
    if (strcmp("-", argv[argn]))
      {
	int	fd;

	fd	= tino_file_open_readA(argv[argc]);
	printfile(fd);
	tino_file_closeA(fd, argv[argc]);
      }
    else
      printfile(0);
}

static void
printline(int argc, char **argv, int argn)
{
  while (argn<argc)
    printansi(argv[argn++]);
  newline();
}

int
main(int argc, char **argv)
{
  int	argn;

  argn	= tino_getopt(argc, argv, 1, 0,
		      TINO_GETOPT_VERSION(PRINTANSI_VERSION)
		      " [-|arg]..",

		      TINO_GETOPT_USAGE
		      "h	this help"
		      ,

		      TINO_GETOPT_INT
		      TINO_GETOPT_DEFAULT
		      "c sec	cycle count of file (only effective with -f)\n"
		      "		In seconds, 0 to disable (inactivity still closes)\n"
		      "		Re-open file after the given period of activity.\n"
		      "		This way you can lock and move it away safely."
		      , &cycle_time,
		      33,

		      TINO_GETOPT_STRING
		      "f name	output to file instead of stdout\n"
		      "		This locks the file and creates it as needed."
		      , &file_name,

		      TINO_GETOPT_FLAG
		      "l	loop mode, read args as lines from files\n"
		      "		Like cat; Arg '-' denotes stdin."
		      , &flag_loop,

		      TINO_GETOPT_STRING
		      "p str	prefix string to prepend to output"
		      , &prefix_string,

		      TINO_GETOPT_STRING
		      "q str	quote output (better empty string detection)\n"
		      "		This string is cycled character by character.\n"
		      "		Empty string quotes with NUL"
		      , &quote_string,

		      TINO_GETOPT_STRING
		      "s str	output suffix after arguments (line seprarator)\n"
		      "		Empty string for NUL, default: NL"
		      , &suffix_string,

		      TINO_GETOPT_STRING
		      "t c	output argument separator (not used with -l)\n"
		      "		Empty string for NUL, default: SPC"
		      , &argument_separator,

		      TINO_GETOPT_FLAG
		      "v	verbosely tell what's going on."
		      , &flag_verbose,

		      TINO_GETOPT_FLAG
		      "z	use NUL as input line terminator (for -l).\n"
		      "		(NUL always is a line terminator)"
		      , &flag_zero,

		      NULL
		      );
  if (argn<=0)
    return 1;

  out	= -1;

  if (flag_loop)
    printfiles(argc, argv, argn);
  else
    printline(argc, argv, argn);

  flushnclose();

  return 0;
}