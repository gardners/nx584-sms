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
#include <string.h>
#include <strings.h>
#include "code_instrumentation.h"


int set_nonblock(int fd);
int write_all(int fd,char *s,int len);

#define MAX_INPUTS 16
int inputs[MAX_INPUTS];
#define IT_UNKNOWN 0
#define IT_CELLMODEM 1
#define IT_NX584SERVERLOG 2
#define IT_TEXTCOMMANDS 3
int input_types[MAX_INPUTS];
int input_count=0;
// Buffers for lines of input being read
#define BUFFER_SIZE 8192
char buffers[MAX_INPUTS][BUFFER_SIZE];
int buffer_lens[MAX_INPUTS];

int armedP=-1;
#define MAX_ZONES 64
#define ZS_UNKNOWN 0
#define ZS_NORMAL 1
#define ZS_FAULT 2
int zoneStates[MAX_ZONES];

int open_input(char *in)
{
  int retVal=-1;
  LOG_ENTRY;

  do {

    if ((!strcmp(in,"stdin"))||(!strcmp(in,"-"))) {
      LOG_NOTE("Registering stdin as input stream");      
      retVal=fileno(stdin);
      set_nonblock(retVal);
      break;
    }
    
    struct stat st;
    int r=stat(in,&st);
    if (r) {
      perror("stat()");
      LOG_ERROR("stat('%s') failed",in);
      retVal=-1;
      break;
    }
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
      // We think it is a serial port, so send ATI to see if it is a cellular modem
      // (we will read the response later)
      write(fd,"ATI\r\n",5);
      
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

int parse_textcommand(int fd,char *line,char *out)
{
  int retVal=-1;
  LOG_ENTRY;

  do {

    int out_len=0;
    
    if (!strcmp(line,"help")) {
      snprintf(out,8192,"Valid commands:\n"
	      "    arm - arm alarm\n"
	      " disarm - disarm alarm\n"
	      " armed? - indicate if alarm armed or not\n"
	      " status - list faulted zones, and if alarm is armed\n"
	      );
      retVal=0;
      break;
    }
    if (!strcmp(line,"status")) {
      switch (armedP) {
      case 0:
	snprintf(&out[out_len],8192-out_len,"Alarm is NOT armed (arm or disarm to be sure).\n");
      case 1:
	snprintf(&out[out_len],8192-out_len,"Alarm IS armed.\n");
      default:
	snprintf(&out[out_len],8192-out_len,"Alarm state unknown.\n");
      }
      out_len=strlen(out);
      // XXX - Work out how many zones we have
      int faults=0;
      int faultZone=-1;
      for(int i=0;i<MAX_ZONES;i++)
	{
	  if (zoneStates[i]==ZS_FAULT) { faults++; faultZone=i; }	  
	}
      if (!faults) {
	snprintf(&out[out_len],8192-out_len,"No zones have faults.\n"); out_len=strlen(out);
      } else if (faults==1) {
	
	snprintf(&out[out_len],8192-out_len,"Zone FAULT in zone #%d\n",faultZone);
	out_len=strlen(out);	
      } else {
	snprintf(&out[out_len],8192-out_len,"The following zones have faults: ");
	out_len=strlen(out);
	for(int i=0;i<MAX_ZONES;i++)
	  {
	    if (zoneStates[i]==ZS_FAULT) {
	      snprintf(&out[out_len],8192-out_len," #%d",i);
	      out_len=strlen(out);
	    }
	    
	  }
	snprintf(&out[out_len],8192-out_len,"\n");
	out_len=strlen(out);	
      }      
      
      retVal=0;
      break;
    }
  } while (0);

  LOG_EXIT;
  return retVal;
}

int parse_line(char *filename,int fd,char *line)
{
  int retVal=IT_UNKNOWN;
  LOG_ENTRY;

  int year,month,day,hour,min,sec,msec,zoneNum;
  char zone_state[8192];

  char out[8192];
    
  do {

    // Allow either , or . as decimal character
    int f=sscanf(line,"%d-%d-%d %d:%d:%d,%d controller INFO Zone %d (%*[^)]) state is %s",
		 &year,&month,&day,&hour,&min,&sec,&msec,&zoneNum,zone_state);
    if (f<9)
      f=sscanf(line,"%d-%d-%d %d:%d:%d.%d controller INFO Zone %d (%*[^)]) state is %s",
	       &year,&month,&day,&hour,&min,&sec,&msec,&zoneNum,zone_state);
      
    if (f==9) {
      LOG_NOTE("Saw controller state message: Zone %d is now '%s'",zoneNum,zone_state);
      if (zoneNum>=0&&zoneNum<MAX_ZONES) {
	if (!strcmp("FAULT",zone_state)) {
	  zoneStates[zoneNum]=ZS_FAULT;
	} else if (!strcmp("NORMAL",zone_state)) {
	  zoneStates[zoneNum]=ZS_NORMAL;
	} else {
	  LOG_NOTE("I don't recognise zone state '%s'",zone_state);
	  zoneStates[zoneNum]=ZS_UNKNOWN;
	}
      }
      retVal=IT_NX584SERVERLOG;
      break;
    }

    // Check if it is a recognised command typed directly in as input
    // (e.g., from stdin).  If so, treat this input as text command interface,
    // and echo output directly back.
    if (!parse_textcommand(fd,line,out)) {
      write_all(fd,out,strlen(out));
      retVal=IT_TEXTCOMMANDS;
      break;
    }
    
    LOG_NOTE("Unrecognised input string '%s'",line);
    retVal=IT_UNKNOWN;
    break;
    
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

    for(int i=0;i<MAX_ZONES;i++) zoneStates[i]=ZS_UNKNOWN;
  
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
      input_types[input_count]=IT_UNKNOWN;
      inputs[input_count++]=fd;
    }
    if (retVal) break;

    LOG_NOTE("%d input streams setup.",input_count);

    fprintf(stderr,
	    "NX584 SMS gateway running.\n"
	    "\n"
	    "If you specified stdin on the command line, you can type commands interactively.\n"
	    );
    
    while (1) {
      // Read from each input type in turn
      int events=0;
      for (int i=0;i<input_count;i++) {
	int r=0;
	if (buffer_lens[i]<(BUFFER_SIZE-1))
	  r=read(inputs[i],&buffers[i][buffer_lens[i]],1);
	if (r>0) {
	  events++;
	  if ((buffers[i][buffer_lens[i]]=='\n')||(buffers[i][buffer_lens[i]]=='\r')) {
	    buffers[i][buffer_lens[i]]=0;
	    LOG_NOTE("Have line of input from '%s': %s",argv[i+1],buffers[i]);
	    input_types[i]=parse_line(argv[i+1],inputs[i],buffers[i]);
	    buffers[i][0]=0;
	    buffer_lens[i]=0;
	  } else	  
	    buffer_lens[i]+=r;
	}
       }
      if (!events) usleep(10000);
    }
    
  } while(0);
  
  LOG_EXIT;
  return retVal;
}
