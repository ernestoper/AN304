/* Copyright 2004,2007,2008,2010 ENSEIRB, INRIA & CNRS
**
** This file is part of the Scotch software package for static mapping,
** graph partitioning and sparse matrix ordering.
**
** This software is governed by the CeCILL-C license under French law
** and abiding by the rules of distribution of free software. You can
** use, modify and/or redistribute the software under the terms of the
** CeCILL-C license as circulated by CEA, CNRS and INRIA at the following
** URL: "http://www.cecill.info".
** 
** As a counterpart to the access to the source code and rights to copy,
** modify and redistribute granted by the license, users are provided
** only with a limited warranty and the software's author, the holder of
** the economic rights, and the successive licensors have only limited
** liability.
** 
** In this respect, the user's attention is drawn to the risks associated
** with loading, using, modifying and/or developing or reproducing the
** software by the user in light of its specific status of free software,
** that may mean that it is complicated to manipulate, and that also
** therefore means that it is reserved for developers and experienced
** professionals having in-depth computer knowledge. Users are therefore
** encouraged to load and test the software's suitability as regards
** their requirements in conditions enabling the security of their
** systems and/or data to be ensured and, more generally, to use and
** operate it in the same conditions as regards security.
** 
** The fact that you are presently reading this means that you have had
** knowledge of the CeCILL-C license and that you accept its terms.
*/
/************************************************************/
/**                                                        **/
/**   NAME       : library_graph_map_view.c                **/
/**                                                        **/
/**   AUTHOR     : Francois PELLEGRINI                     **/
/**                                                        **/
/**   FUNCTION   : This module is the API for the mapping  **/
/**                routines of the libSCOTCH library.      **/
/**                                                        **/
/**   DATES      : # Version 3.2  : from : 19 aug 1998     **/
/**                                 to     20 aug 1998     **/
/**                # Version 3.3  : from : 19 oct 1998     **/
/**                                 to     30 mar 1999     **/
/**                # Version 3.4  : from : 01 nov 2001     **/
/**                                 to     01 nov 2001     **/
/**                # Version 4.0  : from : 13 jan 2004     **/
/**                                 to     30 nov 2006     **/
/**                # Version 5.0  : from : 04 feb 2007     **/
/**                                 to     03 apr 2008     **/
/**                # Version 5.1  : from : 27 jul 2008     **/
/**                                 to     11 aug 2010     **/
/**                                                        **/
/************************************************************/

/*
**  The defines and includes.
*/

#define LIBRARY
#define LIBRARY_GRAPH_MAP_VIEW

#include "module.h"
#include "common.h"
#include "parser.h"
#include "graph.h"
#include "arch.h"
#include "mapping.h"
#include "kgraph.h"
#include "library_mapping.h"
#include "library_graph_map_view.h"
#include "scotch.h"

/************************************/
/*                                  */
/* These routines are the C API for */
/* the mapping routines.            */
/*                                  */
/************************************/

/*+ This routine writes mapping statistics
*** to the given stream.
*** It returns:
*** - 0   : on success.
*** - !0  : on error.
+*/

int
SCOTCH_graphMapView (
const SCOTCH_Graph * const    libgrafptr,
const SCOTCH_Mapping * const  libmappptr,
FILE * const                  stream)
{
  const Graph * restrict    grafptr;
  const Mapping * restrict  mappptr;
  Anum * restrict           parttax;              /* Part array                                   */
  MappingSort * restrict    domntab;              /* Pointer to domain sort array                 */
  ArchDom                   domnfrst;             /* Largest domain in architecture               */
  Anum                      tgtnbr;               /* Number of processors in target topology      */
  Anum                      mapnbr;               /* Number of processors effectively used        */
  Anum                      mapnum;
  double                    mapavg;               /* Average mapping weight                       */
  Gnum                      mapmin;
  Gnum                      mapmax;
  Gnum                      mapsum;               /* (Partial) sum of vertex loads                */
  double                    mapdlt;
  double                    mapmmy;               /* Maximum / average ratio                      */
  Anum * restrict           nghbtab;              /* Table storing neighbors of current subdomain */
  Anum                      nghbnbr;
  Anum                      nghbmin;
  Anum                      nghbmax;
  Anum                      nghbsum;
  Gnum                      vertnum;
  Gnum                      veloval;
  Gnum                      edloval;
  Gnum                      commdist[256];        /* Array of load distribution */
  Gnum                      commload;             /* Total edge load (edge sum) */
  Gnum                      commdilat;            /* Total edge dilation        */
  Gnum                      commexpan;            /* Total edge expansion       */
  Anum                      distmax;
  Anum                      distval;
  Gnum                      diammin;
  Gnum                      diammax;
  Gnum                      diamsum;

  const Gnum * restrict const verttax = ((Graph *) libgrafptr)->verttax;
  const Gnum * restrict const vendtax = ((Graph *) libgrafptr)->vendtax;
  const Gnum * restrict const velotax = ((Graph *) libgrafptr)->velotax;
  const Gnum * restrict const edgetax = ((Graph *) libgrafptr)->edgetax;
  const Gnum * restrict const edlotax = ((Graph *) libgrafptr)->edlotax;

  grafptr = (Graph *) libgrafptr;
  mappptr = &((LibMapping *) libmappptr)->m;

  if ((grafptr->vertnbr == 0) ||                  /* Return if nothing to do */
      (grafptr->edgenbr == 0))
    return (0);

  if (memAllocGroup ((void **) (void *)
                     &domntab, (size_t) ((grafptr->vertnbr + 1) * sizeof (MappingSort)),
                     &parttax, (size_t) (grafptr->vertnbr       * sizeof (Anum)),
                     &nghbtab, (size_t) ((grafptr->vertnbr + 2) * sizeof (Anum)), NULL) == NULL) {
    errorPrint ("SCOTCH_graphMapView: out of memory");
    return     (1);
  }

  memSet (parttax, ~0, grafptr->vertnbr * sizeof (Anum));

  for (vertnum = 0; vertnum < grafptr->vertnbr; vertnum ++) {
    parttax[vertnum]      =
    domntab[vertnum].labl = archDomNum (&mappptr->archdat, mapDomain (mappptr, vertnum + grafptr->baseval));
    domntab[vertnum].peri = vertnum + grafptr->baseval; /* Build inverse permutation */
  }
  domntab[grafptr->vertnbr].labl = ARCHDOMNOTTERM; /* TRICK: avoid testing (i+1)   */
  domntab[grafptr->vertnbr].peri = ~0;            /* Prevent Valgrind from yelling */
  parttax -= grafptr->baseval;                    /* From now on, base part array  */

  intSort2asc2 (domntab, grafptr->vertnbr);       /* Sort domain label array by increasing target labels */

  archDomFrst (&mappptr->archdat, &domnfrst);     /* Get architecture domain    */
  tgtnbr = archDomSize (&mappptr->archdat, &domnfrst); /* Get architecture size */

  mapsum  = 0;
  mapnbr  = 0;
  veloval = 1;                                    /* Assume unweighted vertices */
  for (vertnum = 0; domntab[vertnum].labl != ARCHDOMNOTTERM; vertnum ++) {
    parttax[domntab[vertnum].peri] = mapnbr;      /* Build map of partition parts starting from 0  */
    if (domntab[vertnum].labl != domntab[vertnum + 1].labl) /* TRICK: if new (or end) domain label */
      mapnbr ++;
    if (velotax != NULL)
      veloval = velotax[domntab[vertnum].peri];
    mapsum += veloval;
  }
  mapavg = (mapnbr == 0) ? 0.0L : (double) mapsum / (double) mapnbr;

  mapsum = 0;
  mapmin = GNUMMAX;
  mapmax = 0;
  mapdlt = 0.0L;
  for (vertnum = 0; domntab[vertnum].labl != ARCHDOMNOTTERM; vertnum ++) {
    if (velotax != NULL)
      veloval = velotax[domntab[vertnum].peri];
    mapsum += veloval;

    if (domntab[vertnum].labl != domntab[vertnum + 1].labl) { /* TRICK: if new (or end) domain label */
      if (mapsum < mapmin)
        mapmin = mapsum;
      if (mapsum > mapmax)
        mapmax = mapsum;
      mapdlt += fabs ((double) mapsum - mapavg);
      mapsum = 0;                                 /* Reset domain load sum */
    }
  }
  mapdlt = (mapnbr != 0) ? mapdlt / ((double) mapnbr * mapavg) : 0.0L;
  mapmmy = (mapnbr != 0) ? (double) mapmax / (double) mapavg : 0.0L;

  if (mapnbr > tgtnbr) {                          /* If more subdomains than architecture size */
#ifdef SCOTCH_DEBUG_MAP2
    if (! archVar (&mappptr->archdat)) {          /* If not a variable-sized architecture */
      errorPrint ("SCOTCH_graphMapView: invalid mapping");
      memFree    (domntab);                       /* Free group leader */
      return     (1);
    }
#endif /* SCOTCH_DEBUG_MAP2 */
    tgtnbr = mapnbr;                              /* Assume it is a variable-sized architecture */
  }

  fprintf (stream, "M\tProcessors " GNUMSTRING "/" GNUMSTRING " (%g)\n",
           (Gnum) mapnbr,
           (Gnum) tgtnbr,
           (double) mapnbr / (double) tgtnbr);
  fprintf (stream, "M\tTarget min=" GNUMSTRING "\tmax=" GNUMSTRING "\tavg=%g\tdlt=%g\tmaxavg=%g\n",
           (Gnum) mapmin,
           (Gnum) mapmax,
           mapavg,
           mapdlt,
           mapmmy);

  nghbnbr = 0;
  nghbmin = ANUMMAX;
  nghbmax = 0;
  nghbsum = 0;
  nghbnbr = 0;
  nghbtab[0] = -2;
  for (vertnum = 0; domntab[vertnum].labl != ARCHDOMNOTTERM; vertnum ++) {
    Gnum                edgenum;
    Gnum                edgennd;
    Anum                partnum;

    partnum = parttax[domntab[vertnum].peri];
    for (edgenum = verttax[domntab[vertnum].peri],
         edgennd = vendtax[domntab[vertnum].peri];
         edgenum < edgennd; edgenum ++) {
      Anum                partend;

      partend = parttax[edgetax[edgenum]];
      if ((partend != partnum) &&                 /* If edge is not internal                                      */
          (partend != nghbtab[nghbnbr])) {        /* And neighbor is not sole neighbor or has not just been found */
        Anum                partmin;
        Anum                partmax;

        partmin = 0;
        partmax = nghbnbr;
        while ((partmax - partmin) > 1) {
          Anum                partmed;

          partmed = (partmax + partmin) >> 1;
          if (nghbtab[partmed] > partend)
            partmax = partmed;
          else
            partmin = partmed;
        }
        if (nghbtab[partmin] == partend)          /* If neighboring part found, skip to next neighbor */
          continue;

#ifdef SCOTCH_DEBUG_MAP2
        if (nghbnbr >= (grafptr->vertnbr + 1)) {
          errorPrint ("SCOTCH_graphMapView: internal error");
          return (1);
        }
#endif /* SCOTCH_DEBUG_MAP2 */

        nghbnbr ++;
        for (partmax = nghbnbr; partmax > (partmin + 1); partmax --)
          nghbtab[partmax] = nghbtab[partmax - 1];
        nghbtab[partmin + 1] = partend;           /* Add new neighbor part in the right place */
      }
    }
    if (domntab[vertnum].labl != domntab[vertnum + 1].labl) { /* TRICK: if new (or end) domain label */
      if (nghbnbr < nghbmin)
        nghbmin = nghbnbr;
      if (nghbnbr > nghbmax)
        nghbmax = nghbnbr;
      nghbsum += nghbnbr;

      nghbnbr = 0;
    }
  }

  fprintf (stream, "M\tNeighbors min=" GNUMSTRING "\tmax=" GNUMSTRING "\tsum=" GNUMSTRING "\n",
           (Gnum) nghbmin,
           (Gnum) nghbmax,
           (Gnum) nghbsum);

  memset (commdist, 0, 256 * sizeof (Gnum));      /* Initialize the data */
  commload  =
  commdilat =
  commexpan = 0;

  edloval = 1;
  for (vertnum = grafptr->baseval; vertnum < grafptr->vertnnd; vertnum ++) {
    Gnum                edgenum;

    if (parttax[vertnum] == ~0)                   /* Skip unmapped vertices */
      continue;
    for (edgenum = verttax[vertnum];
         edgenum < vendtax[vertnum]; edgenum ++) {
      if (parttax[edgetax[edgenum]] == ~0)
        continue;
      distval = archDomDist (&mappptr->archdat, mapDomain (mappptr, vertnum), mapDomain (mappptr, edgetax[edgenum]));
      if (edlotax != NULL)                        /* Get edge weight if any */
        edloval = edlotax[edgenum];
      commdist[(distval > 255) ? 255 : distval] += edloval;
      commload  += edloval;
      commdilat += distval;
      commexpan += distval * edloval;
    }
  }

  fprintf (stream, "M\tCommDilat=%f\t(" GNUMSTRING ")\n", /* Print expansion parameters */
           (double) commdilat / grafptr->edgenbr,
           (Gnum) (commdilat / 2));
  fprintf (stream, "M\tCommExpan=%f\t(" GNUMSTRING ")\n",
           ((commload == 0) ? (double) 0.0L
                            : (double) commexpan / (double) commload),
           (Gnum) (commexpan / 2));
  fprintf (stream, "M\tCommCutSz=%f\t(" GNUMSTRING ")\n",
           ((commload == 0) ? (double) 0.0L
                            : (double) (commload - commdist[0]) / (double) commload),
           (Gnum) ((commload - commdist[0]) / 2));
  fprintf (stream, "M\tCommDelta=%f\n",
           (((double) commload  * (double) commdilat) == 0.0L)
           ? (double) 0.0L
           : ((double) commexpan * (double) grafptr->edgenbr) /
             ((double) commload  * (double) commdilat));

  for (distmax = 255; distmax != -1; distmax --)  /* Find longest distance */
    if (commdist[distmax] != 0)
      break;
  for (distval = 0; distval <= distmax; distval ++) /* Print distance histogram */
    fprintf (stream, "M\tCommLoad[" ANUMSTRING "]=%f\n",
             (Anum) distval, (double) commdist[distval] / (double) commload);

  diammin = GNUMMAX;
  diammax = 0;
  diamsum = 0;
  for (mapnum = 0; mapnum < mapnbr; mapnum ++) {
    Gnum                diamval;

    diamval  = graphMapView2 (grafptr, parttax, mapnum);
    diamsum += diamval;
    if (diamval < diammin)
      diammin = diamval;
    if (diamval > diammax)
      diammax = diamval;
  }
  fprintf (stream, "M\tPartDiam\tmin=" GNUMSTRING "\tmax=" GNUMSTRING "\tavg=%lf\n",
           (Gnum) diammin,
           (Gnum) diammax,
           (double) diamsum / (double) mapnbr);

  memFree (domntab);                              /* Free group leader */

  return (0);
}

/*+ This routine computes the pseudo-diameter of
*** the given part.    
*** It returns:
*** - 0   : on success.
*** - !0  : on error.
+*/

static
Gnum
graphMapView2 (
const Graph * const         grafptr,              /*+ Graph      +*/
const Anum * const          parttax,              /*+ Part array +*/
const Anum                  partval)              /*+ Part value +*/
{
  GraphMapViewQueue             queudat;          /* Neighbor queue                         */
  GraphMapViewVertex * restrict vexxtax;          /* Based access to vexxtab                */
  Gnum                          rootnum;          /* Number of current root vertex          */
  Gnum                          vertdist;         /* Vertex distance                        */
  int                           diamflag;         /* Flag set if diameter changed           */
  Gnum                          diambase;         /* Base distance for connected components */
  Gnum                          diamdist;         /* Current diameter distance              */
  Gnum                          diamnum;          /* Vertex which achieves diameter         */
  Gnum                          passnum;          /* Pass number                            */
  const Gnum * restrict         verttax;          /* Based access to vertex array           */
  const Gnum * restrict         vendtax;          /* Based access to vertex end array       */
  const Gnum * restrict         edgetax;

  if (memAllocGroup ((void **) (void *)
                     &queudat.qtab, (size_t) (grafptr->vertnbr * sizeof (Gnum)),
                     &vexxtax,      (size_t) (grafptr->vertnbr * sizeof (GraphMapViewVertex)), NULL) == NULL) {
    errorPrint ("graphMapView2: out of memory");
    return     (-1);
  }

  memSet (vexxtax, 0, grafptr->vertnbr * sizeof (GraphMapViewVertex)); /* Initialize pass numbers */
  edgetax  = grafptr->edgetax;
  verttax  = grafptr->verttax;
  vendtax  = grafptr->vendtax;
  vexxtax -= grafptr->baseval;

  diamnum  = 0;                                   /* Start distances from zero */
  diamdist = 0;
  for (passnum = 1, rootnum = grafptr->baseval; ; passnum ++) { /* For all connected components */
    while ((rootnum < grafptr->vertnbr) &&
           ((vexxtax[rootnum].passnum != 0) ||    /* Find first unallocated vertex */
            (parttax[rootnum] != partval)))
      rootnum ++;
    if (rootnum >= grafptr->vertnbr)              /* Exit if all of graph processed */
      break;

    diambase = ++ diamdist;                       /* Start from previous distance */
    diamnum  = rootnum;                           /* Start from found root        */

    for (diamflag = 1; diamflag -- != 0; passnum ++) { /* Loop if modifications */
      graphMapViewQueueFlush (&queudat);          /* Flush vertex queue         */
      graphMapViewQueuePut   (&queudat, diamnum); /* Start from diameter vertex */
      vexxtax[diamnum].passnum  = passnum;        /* It has been enqueued       */
      vexxtax[diamnum].vertdist = diambase;       /* It is at base distance     */

      do {                                        /* Loop on vertices in queue */
        Gnum                vertnum;
        Gnum                edgenum;

        vertnum  = graphMapViewQueueGet (&queudat); /* Get vertex from queue */
        vertdist = vexxtax[vertnum].vertdist;     /* Get vertex distance     */

        if ((vertdist > diamdist) ||              /* If vertex increases diameter */
            ((vertdist == diamdist) &&            /* Or is at diameter distance   */
             ((vendtax[vertnum] - verttax[vertnum]) < /* With smaller degree  */
              (vendtax[diamnum] - verttax[diamnum])))) {
          diamnum  = vertnum;                     /* Set it as new diameter vertex */
          diamdist = vertdist;
          diamflag = 1;
        }

        vertdist ++;                              /* Set neighbor distance */
        for (edgenum = verttax[vertnum]; edgenum < vendtax[vertnum]; edgenum ++) {
          Gnum                vertend;

          vertend = edgetax[edgenum];
          if ((vexxtax[vertend].passnum < passnum) && /* If vertex not queued yet */
              (parttax[vertend] == partval)) {    /* And of proper part           */
            graphMapViewQueuePut (&queudat, vertend); /* Enqueue neighbor vertex  */
            vexxtax[vertend].passnum  = passnum;
            vexxtax[vertend].vertdist = vertdist;
          }
        }
      } while (! graphMapViewQueueEmpty (&queudat)); /* As long as queue is not empty */
    }
  }

  memFree (queudat.qtab);                         /* Free group leader */

  return (diamdist);
}
