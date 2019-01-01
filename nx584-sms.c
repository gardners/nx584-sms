/*

  NX584 modem interface to SMS controller  
  (C) Copyright Paul Gardner-Stephen 2018-2019

  The idea of this software is to monitor an alarm and SMS interface,
  so that a registered group of users can be notified if the alarm goes
  off, and also so that they can arm and disarm it via their phones, as
  well as enquire the current state.

  The SMS interface is implemented by using the gammu tools for Linux
  to both send and receive SMS messages.

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
#include <stdlib.h>
#include <time.h>
#include "code_instrumentation.h"


int set_nonblock(int fd);
int write_all(int fd,char *s,int len);

char master_pin[1024]="9999";
char nx584_client[1024]="../pynx584/nx584_client";

#define ARM_COMMAND "%s --master %s arm"
#define DISARM_COMMAND "%s --master %s disarm"

#define MAX_INPUTS 16
char *input_files[MAX_INPUTS];
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

int siren=-1;
int armedP=-1;
#define MAX_ZONES 64
#define ZS_UNKNOWN 0
#define ZS_NORMAL 1
#define ZS_FAULT 2
int zoneStates[MAX_ZONES];

time_t siren_on_time=0;
int significant_event=0;

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

#define MAX_USERS 256
char *users[MAX_USERS];
int is_admin[MAX_USERS];
int user_count=0;

char config_file[1024]="/usr/local/etc/nx584-sms.conf";

int save_user_list(void)
{
  FILE *f=fopen(config_file,"w");
  if (!f) {
    LOG_ERROR("Could not open config file '%s' for writing",config_file);
    perror("fopen");
    return -1;
  }

  for(int i=0;i<user_count;i++) {
    if (is_admin[i])
      fprintf(f,"admin %s\n",users[i]);
    else
      fprintf(f,"user %s\n",users[i]);
  }
  
  fclose(f);
  
  return 0;
}

int load_user_list(void)
{
  FILE *f=fopen(config_file,"r");
  if (!f) return -1;

  for(int i=0;i<user_count;i++) if (users[i]) free(users[i]);
  user_count=0;
  
  char line[1024];
  char user[1024];

  line[0]=0; fgets(line,1024,f);
  while (line[0]) {
    if (sscanf(line,"user %s",user)==1) {
      users[user_count]=strdup(user);
      is_admin[user_count++]=0;
    } else if (sscanf(line,"admin %s",user)==1) {
      users[user_count]=strdup(user);
      is_admin[user_count++]=1;
    } else
      LOG_ERROR("Unrecognised line in config file: '%s'",line);
    line[0]=0; fgets(line,1024,f);
  }
  fclose(f);
  
  return 0;
}

int is_admin_or_local(char *phone_number_or_null)
{
  if (!phone_number_or_null) return 1;
  for(int i=0;i<user_count;i++) {
    if (!strcmp(users[i],phone_number_or_null))
      if (is_admin[i]) return 1;
  }
  return 0;
}

int is_authorised(char *phone_number_or_null)
{
  if (!phone_number_or_null) return 1;
  for(int i=0;i<user_count;i++) {
    if (!strcmp(users[i],phone_number_or_null))
      return 1;
  }
  return 0;
}


int add_user(char *phone_number,char *out)
{
  if (phone_number[0]!='+') {
    snprintf(out,1024,"Telephone numbers must be in international format, e.g., +614567898901234567");
    return 1;
  }
    
  if (is_authorised(phone_number)) {
    snprintf(out,1024,"%s is already authorised.",phone_number);
    return 0;
  }
  if (user_count>=MAX_USERS) {
    snprintf(out,1024,"Too many users. Delete one or more and try again.");
    return -1;
  }
  users[user_count]=strdup(phone_number);
  is_admin[user_count++]=0;
  save_user_list();
  snprintf(out,1024,"Added %s to list of authorised users.",phone_number);

  // Send SMS to added user telling them that they have been added
  char cmd[8192];
  snprintf(cmd,8192,"LANG=C gammu sendsms TEXT %s -text \"You are now authorised to remotely control the alarm.  Reply HELP for more information.\"",phone_number);
  printf("[%s]\n",cmd);
  system(cmd);  
  
  return 0;
}

int add_admin(char *phone_number,char *out)
{
  if (phone_number[0]!='+') {
    snprintf(out,1024,"Telephone numbers must be in international format, e.g., +614567898901234567");
    return 1;
  }
  if (is_authorised(phone_number)) {
    snprintf(out,1024,"%s is already authorised. Delete and re-add as admin.",phone_number);
    return 0;
  }
  if (user_count>=MAX_USERS) {
    snprintf(out,1024,"Too many users. Delete one or more and try again.");
    return -1;
  }
  users[user_count]=strdup(phone_number);
  is_admin[user_count++]=1;
  save_user_list();
  snprintf(out,1024,"Added %s to list of administrators.",phone_number);

  char cmd[8192];
  snprintf(cmd,8192,"LANG=C gammu sendsms TEXT %s -text \"You are now authorised to remotely control and administer the alarm.  With great power comes great responsibility. Reply HELP for more information.\"",phone_number);
  printf("[%s]\n",cmd);
  system(cmd);  
  
  return 0;
}

int del_user(char *phone_number,char *out,char *phone_number_or_null)
{
  if (!is_authorised(phone_number)) {
    snprintf(out,1024,"%s was not authorised. Nothing to do.",phone_number);
    return -1;
  }
  if (!strcmp(phone_number,phone_number_or_null)) {
    snprintf(out,1024,"You can't remove yourself as admin user via SMS");
    return -1;
  }
  if (phone_number_or_null) {
    int admin_count=0;
    for(int i=0;i<user_count;i++) if (is_admin[i]) admin_count++;
    if ((admin_count==1)&&is_admin_or_local(phone_number))
      {
	// Can't delete last admin, except from command line interface
	snprintf(out,1024,"You can't remove the last admin user via SMS");
	return -1;
      }
  }
  
  int index=-1;
  for(index=0;index<user_count;index++)
    if (!strcmp(phone_number,users[index])) break;
  free(users[index]); users[index]=NULL;
  for(int i=index;i<(user_count-1);i++) {
    users[i]=users[i+1];
    is_admin[i]=is_admin[i+1];
  }
  snprintf(out,1024,"Removed %s",phone_number);
  return 0;
}

void generate_status_message(char *out,int *out_len,int max_len)
{
  switch (armedP) {
  case 0:
    snprintf(&out[*out_len],max_len-*out_len,"Alarm is NOT armed\n");
    break;
  case 1:
    snprintf(&out[*out_len],max_len-*out_len,"Alarm IS armed.\n");
    break;
  default:
    snprintf(&out[*out_len],max_len-*out_len,"Alarm state unknown (arm or disarm to be sure).\n");
  }
  *out_len=strlen(out);
  switch (siren) {
  case 1:
    snprintf(&out[*out_len],max_len-*out_len,"Siren IS sounding.\n");
    break;
  case 0:
    snprintf(&out[*out_len],max_len-*out_len,"Siren is OFF.\n");
    break;
  default:
    snprintf(&out[*out_len],max_len-*out_len,"I don't know if the siren is on or off.\n");
  }
  *out_len=strlen(out);
  // XXX - Work out how many zones we have
  int faults=0;
  int faultZone=-1;
  for(int i=0;i<MAX_ZONES;i++)
    {
      if (zoneStates[i]==ZS_FAULT) { faults++; faultZone=i; }	  
    }
  if (!faults) {
    snprintf(&out[*out_len],max_len-*out_len,"No zones have faults.\n"); *out_len=strlen(out);
  } else if (faults==1) {
    // XXX - Allow providing names for zones
    snprintf(&out[*out_len],max_len-*out_len,"Zone FAULT in zone #%d\n",faultZone);
    *out_len=strlen(out);	
  } else {
    snprintf(&out[*out_len],max_len-*out_len,"The following zones have faults: ");
    *out_len=strlen(out);
    for(int i=0;i<MAX_ZONES;i++)
      {
	if (zoneStates[i]==ZS_FAULT) {
	  // XXX - Allow providing names for zones
	  snprintf(&out[*out_len],max_len-*out_len," #%d",i);
	  *out_len=strlen(out);
	}
	
      }
    snprintf(&out[*out_len],max_len-*out_len,"\n");
    *out_len=strlen(out);	
  }      
}

int parse_textcommand(int fd,char *line,char *out, char *phone_number_or_local)
{
  int retVal=-1;
  LOG_ENTRY;

  do {

    int out_len=0;
    
    if (!strcasecmp(line,"help")) {
      snprintf(out,8192,"Valid commands:\n"
	       "    arm - arm alarm\n"
	       " disarm - disarm alarm\n"
	       " status - list faulted zones, and if alarm is armed\n"
	       " add <phone number> - add phone number to list of authorised users.\n"
	       " admin <phone number> - add phone number to list of authorised users, with the ability to add and delete others\n"
	       " del <phone number> - delete phone number from list of authorised users.\n"
	       " list - list authorised numbers.\n"
	       );
      retVal=0;
      break;
    }
    if (is_admin_or_local(phone_number_or_local)&&(!strncasecmp(line,"add ",4))) {
      add_user(&line[4],out);
      retVal=0;
      break;
    }    
    if (is_admin_or_local(phone_number_or_local)&&(!strncasecmp(line,"admin ",6))) {
      add_admin(&line[6],out);
      retVal=0;
      break;
    }
    if (is_admin_or_local(phone_number_or_local)&&(!strncasecmp(line,"del ",4))) {
      del_user(&line[4],out,phone_number_or_local);
      retVal=0;
      break;
    }
    if (is_admin_or_local(phone_number_or_local)&&(!strcasecmp(line,"list"))) {
      out[0]=0;
      snprintf(out,8192,"Administrators: ");
      for(int i=0;i<user_count;i++)
	if (is_admin[i]) snprintf(&out[strlen(out)],8192-strlen(out)," %s",users[i]);
      snprintf(&out[strlen(out)],8192-strlen(out),".\n\nUsers: ");      
      for(int i=0;i<user_count;i++)
	if (!is_admin[i]) snprintf(&out[strlen(out)],8192-strlen(out)," %s",users[i]);
      snprintf(&out[strlen(out)],8192-strlen(out),".\n");      
      retVal=0;
      break;
    }

    if (is_authorised(phone_number_or_local)&&(!strcasecmp(line,"disarm"))) {
      char cmd[1024];
      snprintf(cmd,1024,DISARM_COMMAND,nx584_client,master_pin);
      LOG_NOTE("Executing '%s'",cmd);
      int r=system(cmd);
      if (!r) snprintf(out,8192,"Commanded alarm to DISARM.");
      else snprintf(out,8192,"Error #%d requesting alarm to disarm",r);
      retVal=0;
      break;
    }
    if (is_authorised(phone_number_or_local)&&(!strcasecmp(line,"arm"))) {
      char cmd[1024];
      snprintf(cmd,1024,ARM_COMMAND,nx584_client,master_pin);
      LOG_NOTE("Executing '%s'",cmd);
      int r=system(cmd);
      if (!r) snprintf(out,8192,"Commanded alarm to ARM.");
      else snprintf(out,8192,"Error #%d requesting alarm to arm",r);
      retVal=0;
      break;
    }
    if (is_authorised(phone_number_or_local)&&(!strcasecmp(line,"status"))) {
      generate_status_message(out,&out_len,8192);
      
      retVal=0;
      break;
    }
  } while (0);

  LOG_EXIT;
  return retVal;
}

int parse_line(char *origin,int fd,char *line)
{
  int retVal=IT_UNKNOWN;
  LOG_ENTRY;

  int year,month,day,hour,min,sec,msec,zoneNum;
  char zone_state[8192];

  char out[8192];
    
  do {

    // Allow either , or . as decimal character, and also standard python format
    int f=sscanf(line,"%d-%d-%d %d:%d:%d,%d controller INFO Zone %d (%*[^)]) state is %s",
		 &year,&month,&day,&hour,&min,&sec,&msec,&zoneNum,zone_state);
    if (f<9)
      f=sscanf(line,"%d-%d-%d %d:%d:%d.%d controller INFO Zone %d (%*[^)]) state is %s",
	       &year,&month,&day,&hour,&min,&sec,&msec,&zoneNum,zone_state);
    if (f<9) {
      f=sscanf(line,"INFO:controller:Zone %d (%*[^)]) state is %s",
	       &zoneNum,zone_state);
      if (f==2) f=9;
    }      
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

    int partNum=0;
    char part_state[1024];
    
    f=sscanf(line,"%d-%d-%d %d:%d:%d.%d controller INFO Partition %d%*[ ]%[^\n\r]",
	     &year,&month,&day,&hour,&min,&sec,&msec,&partNum,part_state);
    if (f<9) f=sscanf(line,"%d-%d-%d %d:%d:%d,%d controller INFO Partition %d%*[ ]%[^\n\r]",
		      &year,&month,&day,&hour,&min,&sec,&msec,&partNum,part_state);
    if (f<9) {
      f=sscanf(line,"INFO:controller:Partition %d%*[ ]%[^\n\r]",
	       &partNum,part_state);
      if (f==2) f=9;
    }
    if (f==9) {
      if (partNum==1&&(!strcmp(part_state,"armed"))) {
	armedP=1;
	LOG_NOTE("System is armed");
      } else if (partNum==1&&(!strcmp(part_state,"not armed"))) {
	armedP=0; 
	LOG_NOTE("System is not armed");
      } else
	LOG_NOTE("Couldn't work out the partition state message");
      retVal=IT_NX584SERVERLOG;
      break;
    }

    if ((strstr(line,"controller INFO System de-asserts Global Siren on"))
	||(strstr(line,"INFO:controller:System de-asserts Global Siren on")))
      {
	if (!siren_on_time) significant_event++;
	siren=0;
	siren_on_time=0;
	retVal=IT_NX584SERVERLOG;
	break;
      }
    if ((strstr(line,"controller INFO System asserts Global Siren on"))
	||(strstr(line,"INFO:controller:System asserts Global Siren on")))
      {
	siren_on_time=time(0);
	siren=1;
	retVal=IT_NX584SERVERLOG;
	break;
      }
    
    // Ignore all other lines from the NX584 server log
    f=sscanf(line,"%d-%d-%d %d:%d:%d",&year,&month,&day,&hour,&min,&msec);
    if (f==6) {
      retVal=IT_NX584SERVERLOG;
      break;
    }
    
    // Check if it is a recognised command typed directly in as input
    // (e.g., from stdin).  If so, treat this input as text command interface,
    // and echo output directly back.
    // Mark line as fed from local interface, and therefore with admin powers
    if (!parse_textcommand(fd,line,out,
			   // Pass origin as phone number if it isn't indicating stdin
			   ((!origin)||(!strcmp(origin,"-")))?NULL:origin
			   )) {
      if (fd>-1) {
	write_all(fd,out,strlen(out));
	write_all(fd,"\r\n",2);
      }

      if (origin&&strcmp(origin,"-")) {
	// Send reply back by SMS
	char cmd[8192];
	snprintf(cmd,8192,"LANG=C gammu sendsms TEXT %s -text \"%s\"",
		 origin,out);
	printf("[%s]\n",cmd);
	system(cmd);
      }
      
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

time_t last_sms_check_time=0;

int main(int argc,char **argv)
{
  /* We have one or more files/devices to open.
     Each may be one of:
     1. nx584_server output

     We auto-detect which is which, to make user life easy, especially
     since usb-serial adapters by default tend to not have reliable device binding
     by default.  Since the messages from each are quite distinct, this isn't a big
     problem to do.

     UPDATE: By using gammu for SMS, we can avoid much of this problem space.

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

      // Allow nx584_client and master pins to be configured
      int f=sscanf(argv[i],"nx584_client=%s",nx584_client);
      if (f==1) continue;
      f=sscanf(argv[i],"master=%s",master_pin);
      if (f==1) continue;            
      f=sscanf(argv[i],"conf=%s",config_file);
      if (f==1) continue;            
      
      int fd=open_input(argv[i]);
      if (fd==-1) {
	LOG_ERROR("Could not setup input device '%s'",argv[i]);
	retVal=-1;
	break;
      }
      input_files[input_count]=argv[i];
      input_types[input_count]=IT_UNKNOWN;
      inputs[input_count++]=fd;
    }
    if (retVal) break;

    LOG_NOTE("%d input streams setup.",input_count);

    load_user_list();
    LOG_NOTE("%d users registered.",user_count);
    
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
	    LOG_NOTE("Have line of input from '%s': %s",input_files[i],buffers[i]);
	    input_types[i]=parse_line(input_files[i],inputs[i],buffers[i]);
	    buffers[i][0]=0;
	    buffer_lens[i]=0;
	  } else	  
	    buffer_lens[i]+=r;
	}
       }
      if (!events) usleep(10000);

      // Trigger a significant event if the siren has been on more than 10 seconds
      // (this is to avoid triggering a broadcast alert when the siren briefly sounds
      //  during remote arming/disarming).
      if (siren_on_time&&((time(0)-siren_on_time)>=10)) {
	significant_event=1;
	siren_on_time=0;
      }
      if (significant_event) {
	significant_event=0;

	int out_len=0;
	char out[8192];
	sprintf(out,"UNEXPECTED ALARM ACTIVITY: ");
	out_len=strlen(out);
	generate_status_message(out,&out_len,8192);
	
	for(int i=0;i<user_count;i++) {
	  // Send SMS to added user telling them that they have been added
	  char cmd[8192];
	  snprintf(cmd,8192,"LANG=C gammu sendsms TEXT %s -text \"%s. You and %d other(s) have been sent this message. Reply with help for a reminder of commands.\"",users[i],out,user_count-1);
	printf("[%s]\n",cmd);
	system(cmd);  
	
	}
      }
      
      // Check for new messages
      if (last_sms_check_time<time(0)) {

	printf("Getting SMS...\n");
	unlink("/tmp/nx584-sms.txt");
	system("LANG=C gammu getallsms >/tmp/nx584-sms.txt");

	FILE *f=fopen("/tmp/nx584-sms.txt","r");
	if (f) {
	  char sender[1024];
	  char location[1024]="";
	  int isSMS=0;
	  int gotSender=0;
	  char line[1024];
	  line[0]=0; fgets(line,1024,f);
	  while(line[0]) {
	    //	    printf("line>> %s\n",line);
	    if (isSMS==1&&gotSender) {
	      printf("SMS message #%s from '%s' is '%s'\n",location,sender,line);

	      // Run instruction
	      if (is_authorised(sender)) {
		while(line[0]&&line[strlen(line)-1]=='\n') line[strlen(line)-1]=0;
		if (line[0])
		  parse_line(sender,-1,line);
	      } else {
		printf("'%s' is not authorised to use this service.\n",sender);
	      }
	      
	      // Delete SMS message
	      char cmd[8192];
	      snprintf(cmd,8192,"LANG=C gammu deletesms 0 %s",location);
	      system(cmd);	      
	      
	      isSMS=0; gotSender=0; location[0]=0;
	    } else {
	      sscanf(line,"Location %[^,]",location);
	      if (!strncmp("SMS message",line,11)) { isSMS=2; gotSender=0; }
	      if (sscanf(line,"Remote number%*[ ]: \"%[^\"]\"",sender)==1) gotSender=1;
	      if (!strcmp("\n",line)) if (isSMS) isSMS--;
	      if (!isSMS) gotSender=0;
	    }
	    
	    line[0]=0; fgets(line,1024,f);
	  }
	  
	  fclose(f);
	}
	
	last_sms_check_time=time(0);
      }
      
    }
    
  } while(0);
  
  LOG_EXIT;
  return retVal;
}
