/*

NX584 modem interface to SMS controller  
(C) Copyright Paul Gardner-Stephen 2018

The idea of this software is to monitor an alarm and SMS interface,
so that a registered group of users can be notified if the alarm goes
off, and also so that they can arm and disarm it via their phones, as
well as enquire the current state.

For now, it will listen to the cellular modem SMS interface for commands,
and periodically run the nx584_client command to find out the state of
the alarm.  It might also end up monitoring the output of nx584_server as
a more synchonous means of monitoring the alarm, and getting more status 
changes.


This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


*/

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include "code_instrumentation.h"

int open_input(char *in)
{
  int retVal=-1;
  LOG_ENTRY;

  do {
    struct stat st;
    int r=stat(in,&st);
    if (st.st_mode&S_IFREG) {
      LOG_NOTE("'%s' is a regular file",in);
      // Probably a log file, open for input, and seek to the end
      int fd=open(in,O_NONBLOCK,O_RDONLY);
      if (fd==-1) {
	perror("open");
	LOG_ERROR("Could not open '%s' for read",in);
	retVal=-1;
	break;
      }
      // Read to end of file
      off_t r=lseek(fd,0,SEEK_END);
      if (r==-1) {
	LOG_ERROR("Failed to seek to end of '%s'",in);
	perror("lseek()");
	retVal=-1;
	break;
      } else
	LOG_NOTE("Seeked to offset %lld of '%s'",(long long)r,in);
      retVal=fd;
      break;
    } else if (st.st_mode&S_IFCHR) {
      LOG_NOTE("'%s' is a character device",in);
      int fd=open(in,O_NONBLOCK,O_RDWR);
      if (fd==-1) {
	perror("open");
	LOG_ERROR("Could not open '%s' for read and write",in);
	retVal=-1;
	break;
      }
      retVal=fd;
      break;
    } else {
      LOG_ERROR("Input file '%s' is neither regular file nor character device.",in);
      retVal=-1;
      break;
    }
  } while(0);

  LOG_EXIT;
  return retVal;
}

int main(int argc,char **argv)
{
  /* We have one or more files/devices to open.
     Each may be one of:
     1. nx584_server output
     2. serial port to modem

     We auto-detect which is which, to make user life easy, especially
     since usb-serial adapters by default tend to not have reliable device binding
     by default.  Since the messages from each are quite distinct, this isn't a big
     problem to do.
  */

  int retVal=0;
  LOG_ENTRY;

  do {
  
#define MAX_INPUTS 16
    int inputs[MAX_INPUTS];
    int input_count=0;
    
    for(int i=1;i<argc;i++) {
      if (input_count>=MAX_INPUTS) {
	LOG_ERROR("Too many input devices specified");
	retVal=-1;
	break;
      }
      int fd=open_input(argv[i]);
      if (fd==-1) {
	LOG_ERROR("Could not setup input device '%s'",argv[i]);
	retVal=-1;
	break;
      }
      inputs[input_count++]=fd;
    }
    if (retVal) break;

    LOG_NOTE("%d input streams setup.",input_count);
    
  } while(0);
  
  LOG_EXIT;
  return retVal;
}
