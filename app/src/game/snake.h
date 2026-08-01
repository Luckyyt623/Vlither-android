#ifndef SNAKE_H
#define SNAKE_H

#include "body_part.h"
#include "gpt.h"
#include <stdint.h>

typedef struct snake {
  int id;
  int cv;
  int fpos;
  int ftg;
  int fapos;
  int fatg;
  int flpos;
  int fltg;
  int sct;
  int cusk_len;
  int dir;
  int rsc;
  int iiv;
  int edir;
  int ntl_tag_id;
  int ntl_packet_tag_id;
  int vlither_tag_id;
  uint16_t ntl_id;

  float xx;
  float yy;
  float chl;
  float tsp;
  float sfr;
  float sc;
  float ssp;
  float fsp;
  float msp;
  float fx;
  float fy;
  float fa;
  float ehang;
  float wehang;
  float ang;
  float eang;
  float wang;
  float spang;
  float rex;
  float rey;
  float sp;
  float fl;
  float tl;
  float cfl;
  float scang;
  float dead_amt;
  float alive_amt;
  float msl;
  float fam;
  float wsep;
  float sep;
  float fchl;

  /* Smoothed tag headings are kept per snake so tags trail turns naturally
     without sharing state between players or tag systems. */
  float ntl_tag_follow_ang;
  float ntl_tag_follow_mtm;
  float vlither_tag_follow_ang;
  float vlither_tag_follow_mtm;

  float fxs[GD_EEZ];
  float fys[GD_EEZ];
  float fchls[GD_EEZ];
  float fas[GD_AFC];
  float fls[GD_EEZ];

  uint8_t accessory;
  uint8_t cusk_data[MAX_SKIN_CODE_LEN];
  char nk[MAX_NICKNAME_LEN + 1];

  bool cusk;
  bool dead;
  bool ntl_tag_follow_ready;
  bool vlither_tag_follow_ready;

  body_part* pts;
  gpt* gptz;
} snake;

#endif
