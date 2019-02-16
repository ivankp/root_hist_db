select bins, axes.edges
from hist
INNER JOIN axes ON hist.axis = axes.id
where
isp="all" and
photon_cuts="all" and
central_higgs="all" and
nsubjets="all" and
var1="Njets_excl"
