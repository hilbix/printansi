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
 * Revision 1.5  2010-01-12 02:20:02  tino
 * Version with -d and -r
 *
 * Revision 1.4  2009-08-27 18:25:03  tino
 * Current tino/io.h support
 *
 * Revision 1.3  2009-07-31 22:44:30  tino
 * Better error reporting
 *
 * Revision 1.2  2009-07-31 22:30:49  tino
 * Better error reporting
 */

#include "tino/alarm.h"
#include "tino/put.h"
#include "tino/getopt.h"
#include "tino/buf_line.h"
#include "tino/fileerr.h"
#include "tino/sleep.h"

#include "printansi_version.h"

static int	flag_loop, flag_zero, flag_verbose, flag_date, flag_relax;
static char	*prefix_string, *suffix_string, *argument_separator, *quote_string;
static char	*file_name;
static int	cycle_time;

static int	out=-1, quotes;

static int	within_line;

static int	arm, tick, opentime;

static int	havetime;
static char	now[20];

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
  int oarm;

  if (out<0)
    return;

  /* prevent reentrancy
   */
  oarm	= arm;
  arm	= 0;

  tino_io_write(out, NULL, 1);
  tino_file_flush_fdE(out);
  if (file_name)
    {
      tino_file_closeA(out, file_name);
      verbose("closed file %s", file_name);
    }
  else
    verbose("closed STDOUT");

  out	= -1;

  tino_relax();

  arm	= oarm;
}

static int
alarm_cb(void *user, long delta, time_t now, long run)
{
  opentime++;
  havetime=0;

  if (!arm)
    return 0;	/* not arms, no fun	*/

  if (out<0)
    return 1;	/* if we have no file we do not need this alarm as it will be re-inserted	*/

  switch (++tick)
    {
    case 2:
      arm=0;
      tino_io_write(out, NULL, 1);
      arm=1;
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
  int	oarm;

  if (out>=0)
    return;

  oarm	= arm;
  arm	= 0;

  tino_alarm_set(1, alarm_cb, NULL);
  if (!file_name)
    {
      out	= tino_io_fd(1, "stdout");
      verbose("opened STDOUT");
    }
  else
    {
      for (;;)
	{
	  int	fd;

	  fd	= tino_file_open_createA(file_name, O_RDWR|O_APPEND, 0600);

          verbose("try to lock file %s", file_name);
	  TINO_ALARM_RUN();

	  /* Lock file
	   */
	  if (!tino_file_lockI(fd, 1, 1))
	    {
	      out	= tino_io_fd(fd, file_name);
              verbose("appending to %s", file_name);
	      opentime	= 0;
	      break;
	    }
	  tino_file_closeE(fd);
	  /* loop 	*/
	}
    }
  tino_io_prep(out);	/* create the IO buffer	*/

  arm	= oarm;
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
      if (flag_date)
        {
          if (!havetime)
	    {
	      time_t tmp;

              time(&tmp);
	      strftime(now,sizeof now,"[%Y%m%d-%H%M%S]", gmtime(&tmp));
              havetime=1;
            }
	  outs(now);
	}
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
  tino_put_ansi(out, s, flag_relax ? "" : NULL);
  quote();
  within_line	= 1;
}

/* In future this will use io.h too, of course, as soon as input is
 * implemented.
 */
static void
printfile(int fd, const char *name)
{
  TINO_BUF	 buf;
  const char	*s;

  tino_buf_initO(&buf);
  while ((s=tino_buf_line_read(&buf, fd, flag_zero ? 0 : '\n'))!=0)
    {
      arm	= 0;
      if (*s)
	{
	  printansi(s);
	  newline();
	}
      arm	= 1;
      tick	= 0;
      if (cycle_time && opentime>cycle_time)
	flushnclose();
    }
  if (errno)
    tino_err("ETTPA100E read error in %s", name);
}

static void
printfiles(int argc, char **argv, int argn)
{
  for (; argn<argc; argn++)
    if (strcmp("-", argv[argn]))
      {
	int	fd;

	fd	= tino_file_open_readA(argv[argn]);
	printfile(fd, argv[argn]);
	tino_file_closeA(fd, argv[argn]);
      }
    else
      printfile(0, argv[argn]);
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

		      TINO_GETOPT_FLAG
		      "d	prefix line with UTC date stamp [YYYYMMDD-HHMMSS]\n"
		      "		This comes before -p"
		      , &flag_date,

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

		      TINO_GETOPT_FLAG
		      "r	relaxed output, do not escape spaces nor tic (')."
		      , &flag_relax,

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
