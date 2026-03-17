//FORMAT   JOB (2),'FORMAT',CLASS=A,REGION=0K,MSGCLASS=A                        
//FORMAT EXEC PGM=FORMAT,PARM='-B 4096'                                         
//STEPLIB  DD DISP=SHR,DSN=HTTPD.LINKLIB                                        
//SYSTERM  DD SYSOUT=*              STDERR                                      
//SYSPRINT DD SYSOUT=*              STDOUT                                      
//SYSIN    DD DUMMY                 STDIN                                       
//SYSABEND DD SYSOUT=*                                                          
//SYSENV   DD *                                                                 
/*                                                                              
//DISKFILE DD DISP=OLD,DSN=UFSD.SCRATCH                                         
//                                                                              
