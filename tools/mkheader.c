/*
 * mkheader.c - create a file header.h containing values for the OS header. 
 *
 * Copyright (c) 2001 EmuTOS Development Team
 *
 * Authors:
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * usage: mkheader <code>,
 * where code is a country code in the table below.
 * creates the file HEADERNAME #defined below.
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#define VERSION "0.1a"
#define HEADERNAME "bios/header.h"

/*
 * The list below should be kept in sync with the list in bios/country.h
 * note: the two letter codes are my invention.
 */

struct country_info {
  int number;
  char *code;
  char *name;
};

struct country_info countries[] = {
/* defined in the compendium */
  {  0, "us", "USA" },
  {  1, "de", "Germany" },
  {  2, "fr", "France" },
  {  3, "uk", "United Kingdom" },
  {  4, "es", "Spain" },
  {  5, "it", "Italy" },
  {  6, "se", "Sweden" },
  {  7, "sf", "Switzerland (French)" },
  {  8, "sg", "Switzerland (German)" },
/* Given by Petr in mail */
  {  9, "tr", "Turkey" }, 
  { 10, "fi", "Finland" }, 
  { 11, "no", "Norway" },
  { 12, "dk", "Denmark" }, 
  { 13, "sa", "Saudi Arabia" },
  { 14, "nl", "Holland" }, 
  { 15, "cz", "Czech Republic" },  
  { 16, "hu", "Hungary" },  
  { 17, "sk", "Slovak Republic" },
};
  
  
int get_country_number(char *s)
{
  int i, n;
  
  n = sizeof(countries) / sizeof(*countries);
  for(i = 0 ; i < n ; i++) {
    struct country_info *c = &countries[i];
    if(!strcmp(s, c->code)) {
      return c->number;
    }
  }
  return -1;
}

void dump_countries(FILE *f)
{
  int i, n;
  
  n = sizeof(countries) / sizeof(*countries);
  fprintf(f, "number\tcode\tname\n");
  for(i = 0 ; i < n ; i++) {
    struct country_info *c = &countries[i];
    fprintf(f, "%d\t%s\t%s\n", c->number, c->code, c->name);
  }
}

/*
 * fatal
 */

void fatal(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "Fatal: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

/*
 * time manipulations
 */

char * now(time_t *t)
{
  return ctime(t);  
}

char * os_date(char *buf, time_t *t)
{
  struct tm *p = localtime(t);
  sprintf(buf, "0x%02d%02d%04d", p->tm_mon + 1, p->tm_mday, p->tm_year+1900);
  return buf;
}

char * os_dosdate(char *buf, time_t *t)
{
  unsigned int u;
  struct tm *p = localtime(t);
  
  u = (p->tm_mday) | ((p->tm_mon + 1) << 5) | ((p->tm_year - 80) << 9);
  sprintf(buf, "0x%04x", u);
  return buf;
}

char * os_pal(char *buf, int country_number)
{
  sprintf(buf, "0x%04x", country_number<<1);
  return buf;
}


void make_header(int country_number)
{
  FILE *f;
  time_t t;
  char buf[20];
  
  t = time(0);
  f = fopen(HEADERNAME, "w");
  
  if(f == NULL) fatal("couldn't create " HEADERNAME);
  fprintf(f, "\
/*\n\
 * header.h - definitions for the OS header in startup.S\n\
 *\n\
 * This file was generated by mkheader on %s\
 * Do not change this file!\n\
 */\n\n", now(&t));

  fprintf(f, "\
/* the build date in Binary-Coded Decimal */\n\
#define OS_DATE %s\n\n", os_date(buf, &t));
  fprintf(f, "\
/* the country number << 1 and the PAL/NTSC flag */\n\
#define OS_PAL %s\n\n", os_pal(buf, country_number));
  fprintf(f, "\
/* the country number only (used by country.c) */\n\
#define OS_COUNTRY %d\n\n", country_number);
  fprintf(f, "\
/* the build date in GEMDOS format */\n\
#define OS_DOSDATE %s\n\n", os_dosdate(buf, &t));

  fclose(f);
}


int main(int argc, char **argv) 
{
  int i;
  
  if(argc != 2) goto usage;
  if(!strcmp(argv[1], "--version")) {
    printf("version " VERSION "\n");
    exit(0);
  } else {
    i = get_country_number(argv[1]);
    if(i == -1) goto usage;
    make_header(i);
    exit(0);
  }
usage:
  fprintf(stderr, "\
Usage: mkheader CODE\n\
  creates the file " HEADERNAME ", setting the country according to CODE, \n\
  where CODE is a code in the table below:\n");
  dump_countries(stderr);
  exit(1);
}

