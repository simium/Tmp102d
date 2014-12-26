/**************************************************************************
 * 
 * Read temperature from TMP102 chip with I2C and write it to a log file. 
 *       
 * Copyright (C) 2014 Jaakko Koivuniemi.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************
 *
 * Tue Feb 25 21:28:38 CET 2014
 * Edit: Fri Dec 26 11:09:47 CET 2014
 *
 * Jaakko Koivuniemi
 **/

#include <stdio.h>
#include <stdlib.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

const int version=20141226; // program version
int tempint1=300; // temperature reading interval [s]
int tempint2=0; // second temperature reading interval [s]
int tempint3=0; // third temperature reading interval [s]
int tempint4=0; // fourth temperature reading interval [s]

const char tdatafile1[200]="/var/lib/tmp102d/temperature";
const char tdatafile2[200]="/var/lib/tmp102d/temperature2";
const char tdatafile3[200]="/var/lib/tmp102d/temperature3";
const char tdatafile4[200]="/var/lib/tmp102d/temperature4";

const char i2cdev[100]="/dev/i2c-1";
int address1=0x4a;
int address2=0;
int address3=0;
int address4=0;

const int  i2lockmax=10; // maximum number of times to try lock i2c port  

const char confile[200]="/etc/tmp102d_config";

const char pidfile[200]="/var/run/tmp102d.pid";

int loglev=3;
const char logfile[200]="/var/log/tmp102d.log";
char message[200]="";

void logmessage(const char logfile[200], const char message[200], int loglev, int msglev)
{
  time_t now;
  char tstr[25];
  struct tm* tm_info;
  FILE *log;

  time(&now);
  tm_info=localtime(&now);
  strftime(tstr,25,"%Y-%m-%d %H:%M:%S",tm_info);
  if(msglev>=loglev)
  {
    log=fopen(logfile, "a");
    if(NULL==log)
    {
      perror("could not open log file");
    }
    else
    { 
      fprintf(log,"%s ",tstr);
      fprintf(log,"%s\n",message);
      fclose(log);
    }
  }
}

void read_config()
{
  FILE *cfile;
  char *line=NULL;
  char par[20];
  float value;
  unsigned int addr;
  size_t len;
  ssize_t read;

  cfile=fopen(confile, "r");
  if(NULL!=cfile)
  {
    sprintf(message,"Read configuration file");
    logmessage(logfile,message,loglev,4);

    while((read=getline(&line,&len,cfile))!=-1)
    {
       if(sscanf(line,"%s %f",par,&value)!=EOF) 
       {
          if(strncmp(par,"LOGLEVEL",8)==0)
          {
             loglev=(int)value;
             sprintf(message,"Log level set to %d",(int)value);
             logmessage(logfile,message,loglev,4);
          }
          if(strncmp(par,"I2CADDR1",8)==0)
          {
             if(sscanf(line,"%s 0x%x",par,&addr)!=EOF)
             { 
                sprintf(message,"First TMP102 chip address set to 0x%x",addr);
                logmessage(logfile,message,loglev,4);
                address1=(int)addr;
             }
          }
          if(strncmp(par,"I2CADDR2",8)==0)
          {
             if(sscanf(line,"%s 0x%x",par,&addr)!=EOF)
             { 
                sprintf(message,"Second TMP102 chip address set to 0x%x",addr);
                logmessage(logfile,message,loglev,4);
                address2=(int)addr;
             }
          }
          if(strncmp(par,"I2CADDR3",8)==0)
          {
             if(sscanf(line,"%s 0x%x",par,&addr)!=EOF)
             { 
                sprintf(message,"Third TMP102 chip address set to 0x%x",addr);
                logmessage(logfile,message,loglev,4);
                address3=(int)addr;
             }
          }
          if(strncmp(par,"I2CADDR4",8)==0)
          {
             if(sscanf(line,"%s 0x%x",par,&addr)!=EOF)
             { 
                sprintf(message,"Fourth TMP102 chip address set to 0x%x",addr);
                logmessage(logfile,message,loglev,4);
                address4=(int)addr;
             }
          }
          if(strncmp(par,"TEMPINT1",8)==0)
          {
             tempint1=(int)value;
             sprintf(message,"First temperature reading interval set to %d s",(int)value);
             logmessage(logfile,message,loglev,4);
          }
          if(strncmp(par,"TEMPINT2",8)==0)
          {
             tempint2=(int)value;
             sprintf(message,"Second temperature reading interval set to %d s",(int)value);
             logmessage(logfile,message,loglev,4);
          }
          if(strncmp(par,"TEMPINT3",8)==0)
          {
             tempint3=(int)value;
             sprintf(message,"Third temperature reading interval set to %d s",(int)value);
             logmessage(logfile,message,loglev,4);
          }
          if(strncmp(par,"TEMPINT4",8)==0)
          {
             tempint4=(int)value;
             sprintf(message,"Fourth temperature reading interval set to %d s",(int)value);
             logmessage(logfile,message,loglev,4);
          }
       }
    }
    fclose(cfile);
  }
  else
  {
    sprintf(message, "Could not open %s", confile);
    logmessage(logfile, message, loglev,4);
  }
}

int cont=1; /* main loop flag */

// read data with i2c from address, length is the number of bytes to read 
// return: -1=open failed, -2=lock failed, -3=bus access failed, 
// -4=i2c slave reading failed
int read_data(int address, int length)
{
  int rdata=0;
  int fd,rd;
  int cnt=0;
  unsigned char buf[10];

  if((fd=open(i2cdev, O_RDWR)) < 0) 
  {
    sprintf(message,"Failed to open i2c port");
    logmessage(logfile,message,loglev,4);
    return -1;
  }

  rd=flock(fd, LOCK_EX|LOCK_NB);

  cnt=i2lockmax;
  while((rd==1)&&(cnt>0)) // try again if port locking failed
  {
    sleep(1);
    rd=flock(fd, LOCK_EX|LOCK_NB);
    cnt--;
  }
  if(rd)
  {
    sprintf(message,"Failed to lock i2c port");
    logmessage(logfile,message,loglev,4);
    close(fd);
    return -2;
  }

  if(ioctl(fd, I2C_SLAVE, address) < 0) 
  {
    sprintf(message,"Unable to get bus access to talk to slave");
    logmessage(logfile,message,loglev,4);
    close(fd);
    return -3;
  }

  if(length==1)
  {
     if(read(fd, buf,1)!=1) 
     {
       sprintf(message,"Unable to read from slave, exit");
       logmessage(logfile,message,loglev,4);
       cont=0;
       close(fd);
       return -4;
     }
     else 
     {
       sprintf(message,"Receive 0x%02x",buf[0]);
       logmessage(logfile,message,loglev,1); 
       rdata=buf[0];
     }
  } 
  else if(length==2)
  {
     if(read(fd, buf,2)!=2) 
     {
       sprintf(message,"Unable to read from slave, exit");
       logmessage(logfile,message,loglev,4);
       cont=0;
       close(fd);
       return -4;
     }
     else 
     {
       sprintf(message,"Receive 0x%02x%02x",buf[0],buf[1]);
       logmessage(logfile,message,loglev,1);  
       rdata=256*buf[0]+buf[1];
     }
  }
  else if(length==4)
  {
     if(read(fd, buf,4)!=4) 
     {
       sprintf(message,"Unable to read from slave, exit");
       logmessage(logfile,message,loglev,4);
       cont=0;
       close(fd);
       return -4;
     }
     else 
     {
        sprintf(message,"Receive 0x%02x%02x%02x%02x",buf[0],buf[1],buf[2],buf[3]);
        logmessage(logfile,message,loglev,1);  
        rdata=16777216*buf[0]+65536*buf[1]+256*buf[2]+buf[3];
     }
  }

  close(fd);

  return rdata;
}

void write_temp(float t, int addr)
{
  FILE *tfile=NULL;

  if(addr==address1) tfile=fopen(tdatafile1, "w");
  else if(addr==address2) tfile=fopen(tdatafile2, "w"); 
  else if(addr==address3) tfile=fopen(tdatafile3, "w"); 
  else if(addr==address4) tfile=fopen(tdatafile4, "w"); 

  if(NULL==tfile)
  {
    sprintf(message,"could not write to file");
    logmessage(logfile,message,loglev,4);
  }
  else
  { 
    fprintf(tfile,"%2.1f",t);
    fclose(tfile);
  }
}

void read_temp(int addr)
{
  short value=0;
  float temp=0;

  value=(short)(read_data(addr,2));
  value/=16;
  temp=(float)(value*0.0625);

  if(cont==1)
  {
    if(addr==address1) sprintf(message,"T1=%f C",temp);
    else if(addr==address2) sprintf(message,"T2=%f C",temp);
    else if(addr==address3) sprintf(message,"T3=%f C",temp);
    else if(addr==address4) sprintf(message,"T4=%f C",temp);
    logmessage(logfile,message,loglev,4);
    write_temp(temp,addr);
  }
}


void stop(int sig)
{
  sprintf(message,"signal %d catched, stop",sig);
  logmessage(logfile,message,loglev,4);
  cont=0;
}

void terminate(int sig)
{
  sprintf(message,"signal %d catched",sig);
  logmessage(logfile,message,loglev,4);

  sleep(1);
  strcpy(message,"stop");
  logmessage(logfile,message,loglev,4);

  cont=0;
}

void hup(int sig)
{
  sprintf(message,"signal %d catched",sig);
  logmessage(logfile,message,loglev,4);
}


int main()
{  
  int ok=0;

  sprintf(message,"tmp102d v. %d started",version); 
  logmessage(logfile,message,loglev,4);

  signal(SIGINT,&stop); 
  signal(SIGKILL,&stop); 
  signal(SIGTERM,&terminate); 
  signal(SIGQUIT,&stop); 
  signal(SIGHUP,&hup); 

  read_config();

  pid_t pid, sid;
        
  pid=fork();
  if(pid<0) 
  {
    exit(EXIT_FAILURE);
  }

  if(pid>0) 
  {
    exit(EXIT_SUCCESS);
  }

  umask(0);

  /* Create a new SID for the child process */
  sid=setsid();
  if(sid<0) 
  {
    strcpy(message,"failed to create child process"); 
    logmessage(logfile,message,loglev,4);
    exit(EXIT_FAILURE);
  }
        
  if((chdir("/")) < 0) 
  {
    strcpy(message,"failed to change to root directory"); 
    logmessage(logfile,message,loglev,4);
    exit(EXIT_FAILURE);
  }
        
  /* Close out the standard file descriptors */
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  FILE *pidf;
  pidf=fopen(pidfile,"w");

  if(pidf==NULL)
  {
    sprintf(message,"Could not open PID lock file %s, exiting", pidfile);
    logmessage(logfile,message,loglev,4);
    exit(EXIT_FAILURE);
  }

  if(flock(fileno(pidf),LOCK_EX||LOCK_NB)==-1)
  {
    sprintf(message,"Could not lock PID lock file %s, exiting", pidfile);
    logmessage(logfile,message,loglev,4);
    exit(EXIT_FAILURE);
  }

  fprintf(pidf,"%d\n",getpid());
  fclose(pidf);

  int unxs=(int)time(NULL); // unix seconds
  int nxtemp1=unxs; // next time to read temperature
  int nxtemp2=unxs; // next time to read second temperature
  int nxtemp3=unxs; // next time to read third temperature
  int nxtemp4=unxs; // next time to read fourth temperature

  while(cont==1)
  {
    unxs=(int)time(NULL); 

    if(((unxs>=nxtemp1)||((nxtemp1-unxs)>tempint1))&&(tempint1>0)&&(address1>=0x48)&&(address1<=0x4B)) 
    {
      nxtemp1=tempint1+unxs;
      read_temp(address1);
// optional script to insert the data to local database
//          ok=system("/usr/sbin/insert-tmp102.sh");
    }

    if(((unxs>=nxtemp2)||((nxtemp2-unxs)>tempint2))&&(tempint2>0)&&(address2>=0x48)&&(address2<=0x4B)) 
    {
      nxtemp2=tempint2+unxs;
      read_temp(address2);
//          ok=system("/usr/sbin/insert-tmp102b.sh");
    }

    if(((unxs>=nxtemp3)||((nxtemp3-unxs)>tempint3))&&(tempint3>0)&&(address3>=0x48)&&(address3<=0x4B)) 
    {
      nxtemp3=tempint3+unxs;
      read_temp(address3);
//          ok=system("/usr/sbin/insert-tmp102c.sh");
    }

    if(((unxs>=nxtemp4)||((nxtemp4-unxs)>tempint4))&&(tempint4>0)&&(address4>=0x48)&&(address4<=0x4B)) 
    {
      nxtemp4=tempint4+unxs;
      read_temp(address4);
//          ok=system("/usr/sbin/insert-tmp102d.sh");
    }

    sleep(1);
  }

  strcpy(message,"remove PID file");
  logmessage(logfile,message,loglev,4);
  ok=remove(pidfile);

  return ok;
}
