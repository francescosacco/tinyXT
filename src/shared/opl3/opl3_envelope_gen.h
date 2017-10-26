/*
 * File: opl3_envelope_gen.h
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

class OPL3_EnvelopeGenerator_t
{
public:

  OPL3_EnvelopeGenerator_t();
  ~OPL3_EnvelopeGenerator_t();

  void setActualSustainLevel(int sl);

  void setTotalLevel(int tl);

  void setAtennuation(int f_number, int block, int ksl);

  void setActualAttackRate(int attackRate, int ksr, int keyScaleNumber);

  void setActualDecayRate(int decayRate, int ksr, int keyScaleNumber);

  void setActualReleaseRate(int releaseRate, int ksr, int keyScaleNumber);

  double getEnvelope(int egt, int am);

  void keyOn(void);

  void keyOff(void);

private:

  enum Stage_t
  {
    STAGE_ATTACK,
    STAGE_DECAY,
    STAGE_SUSTAIN,
    STAGE_RELEASE,
    STAGE_OFF
  };

  Stage_t stage;

  int actualAttackRate, actualDecayRate, actualReleaseRate;
  double xAttackIncrement, xMinimumInAttack;
  double dBdecayIncrement;
  double dBreleaseIncrement;
  double attenuation, totalLevel, sustainLevel;
  double x, envelope;

  int calculateActualRate(int rate, int ksr, int keyScaleNumber);

  double dBtoX(double dB);

  double percentageToDB(double percentage);

  double percentageToX(double percentage);
};


