#!/usr/bin/env python

import sqlite3, struct

db = sqlite3.connect('../test.db')

hs = db.execute('''
select type, jet, var1, var2, edges, bins
from hist
INNER JOIN axes ON hist.axis = axes.id
where
isp="all" and
photon_cuts="all" and
central_higgs="all" and
nsubjets="all" and
var1="Njets_excl"
''').fetchall()

print hs

