/*
 * File: opl3_emulator.h
 *
 * Software implementation of the Yamaha YMF262 sound generator in C++.
 * Copyright (C) 2014 Julian Olds
 *
 * Developed from OPL3.java Copyright (C) 2008 Robson Cozendey <robson@cozendey.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */


#ifndef __OPL3_EMULATOR_H
#define __OPL3_EMULATOR_H

class OPL3_Emulator_t
{
public:

  OPL3_Emulator_t();
  ~OPL3_Emulator_t();

  unsigned char ReadRegister(int array, int address);

  void WriteRegister(int array, int address, unsigned char data);

  void GetSamples(unsigned char *Data, int SampleRate, int NSamples);

protected:

  unsigned char Registers[0x200];

  int nts;
  int dam;
  int dvb;
  int ryt;
  int bd;
  int sd;
  int tom;
  int tc;
  int hh;
  int _new;
  int connectionsel;

  int vibratoIndex;
  int tremoloIndex;


  void initOperators(void);
  void initChannels2op(void);
  void initChannels4op(void);
  void initRhythmChannels(void);
  void initChannels(void);
};

#endif // __OPL3_EMULATOR_H
