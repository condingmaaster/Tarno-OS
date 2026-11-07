// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sqlite3.h>
static const char *path="/tmp/thos-sqlite-lock-test.db";
static void exec_ok(sqlite3*d,const char*s){char*e=0;int r=sqlite3_exec(d,s,0,0,&e);if(r!=SQLITE_OK)printf("sqlite-FAIL code=%d sql=%s error=%s\n",r,s,e?e:sqlite3_errmsg(d));sqlite3_free(e);assert(r==SQLITE_OK);}
static void value(sqlite3*d,const char*expected){sqlite3_stmt*s=0;assert(sqlite3_prepare_v2(d,"SELECT value FROM data WHERE id=1",-1,&s,0)==SQLITE_OK);assert(sqlite3_step(s)==SQLITE_ROW&&!strcmp((const char*)sqlite3_column_text(s,0),expected));assert(sqlite3_step(s)==SQLITE_DONE&&sqlite3_finalize(s)==SQLITE_OK);}
static sqlite3*open_db(int create){sqlite3*d=0;assert(sqlite3_open_v2(path,&d,SQLITE_OPEN_READWRITE|(create?SQLITE_OPEN_CREATE:0),0)==SQLITE_OK);sqlite3_busy_timeout(d,50);return d;}
static void child(const char*self,const char*mode){pid_t p=fork();assert(p>=0);if(!p){execl(self,self,mode,(char*)0);_exit(99);}int s;assert(waitpid(p,&s,0)==p&&WIFEXITED(s)&&WEXITSTATUS(s)==0);}
int main(int argc,char**argv){setbuf(stdout,0);
 if(argc==2){sqlite3*d=open_db(0);
  if(!strcmp(argv[1],"--read-before"))value(d,"before");
  else if(!strcmp(argv[1],"--write-blocked")){int r=sqlite3_exec(d,"BEGIN IMMEDIATE",0,0,0);printf("sqlite-writer-contention=%d expected=%d\n",r,SQLITE_BUSY);assert(r==SQLITE_BUSY);}
  else if(!strcmp(argv[1],"--commit-blocked")){exec_ok(d,"BEGIN IMMEDIATE; UPDATE data SET value='must roll back' WHERE id=1");int r=sqlite3_exec(d,"COMMIT",0,0,0);printf("sqlite-reader-contention=%d expected=%d\n",r,SQLITE_BUSY);assert(r==SQLITE_BUSY);exec_ok(d,"ROLLBACK");}
  else if(!strcmp(argv[1],"--commit"))exec_ok(d,"BEGIN IMMEDIATE; UPDATE data SET value='child committed' WHERE id=1; COMMIT");
  else if(!strcmp(argv[1],"--verify"))value(d,"child committed");else return 2;
  assert(sqlite3_close(d)==SQLITE_OK);return 0;
 }
 unlink(path);unlink("/tmp/thos-sqlite-lock-test.db-journal");printf("sqlite-version=%s journal=DELETE\n",sqlite3_libversion());
 sqlite3*d=open_db(1);exec_ok(d,"PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; CREATE TABLE data(id INTEGER PRIMARY KEY,value TEXT); INSERT INTO data VALUES(1,'before')");
 exec_ok(d,"BEGIN IMMEDIATE; UPDATE data SET value='uncommitted' WHERE id=1");child(argv[0],"--read-before");child(argv[0],"--write-blocked");exec_ok(d,"ROLLBACK");value(d,"before");
 exec_ok(d,"BEGIN");value(d,"before");child(argv[0],"--commit-blocked");exec_ok(d,"ROLLBACK");value(d,"before");
 child(argv[0],"--commit");value(d,"child committed");assert(sqlite3_close(d)==SQLITE_OK);child(argv[0],"--verify");
 unlink(path);unlink("/tmp/thos-sqlite-lock-test.db-journal");puts("sqlite-lock-test-done: real separate-process writer/reader exclusion, rollback, commit and persisted bytes; DELETE journal only");return 0;
}
