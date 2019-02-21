#!/bin/bash

./bin/make_db \
  ../H2jB_{eft,mtop}_antikt4.root \
  -o ../test.db \
  -l proc type jet weight isp photon_cuts central_higgs nsubjets var1 var2

