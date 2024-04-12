/** \ingroup rpmdep
 * \file lib/rpmal.c
 */

#include "system.h"

#include <unordered_map>

#include <rpm/rpmte.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmstring.h>
#include <rpm/rpmstrpool.h>

#include "rpmal.h"
#include "misc.h"
#include "rpmte_internal.h"
#include "rpmds_internal.h"
#include "rpmfi_internal.h"
#include "rpmts_internal.h"

#include "debug.h"

typedef struct availablePackage_s * availablePackage;
typedef int rpmalNum;

/** \ingroup rpmdep
 * Info about a single package to be installed.
 */
struct availablePackage_s {
    rpmte p;                    /*!< transaction member */
    rpmds provides;		/*!< Provides: dependencies. */
    rpmds obsoletes;		/*!< Obsoletes: dependencies. */
    rpmfiles fi;		/*!< File info set. */
};

/** \ingroup rpmdep
 * A single available item (e.g. a Provides: dependency).
 */
typedef struct availableIndexEntry_s {
    rpmalNum pkgNum;	        /*!< Containing package index. */
    unsigned int entryIx;	/*!< Dependency index. */
} * availableIndexEntry;

typedef std::unordered_map<rpmsid,availableIndexEntry_s> rpmalDepHash;

typedef struct availableIndexFileEntry_s {
    rpmsid dirName;
    rpmalNum pkgNum;	        /*!< Containing package index. */
    unsigned int entryIx;	/*!< Dependency index. */
} * availableIndexFileEntry;

typedef std::unordered_multimap<rpmsid,availableIndexFileEntry_s> rpmalFileHash;

/** \ingroup rpmdep
 * Set of available packages, items, and directories.
 */
struct rpmal_s {
    rpmstrPool pool {};		/*!< String pool */
    availablePackage list {};	/*!< Set of packages. */
    rpmalDepHash *providesHash {};
    rpmalDepHash *obsoletesHash {};
    rpmalFileHash *fileHash {};
    int delta {};		/*!< Delta for pkg list reallocation. */
    int size {};		/*!< No. of pkgs in list. */
    int alloced {};		/*!< No. of pkgs allocated for list. */
    rpmtransFlags tsflags {};	/*!< Transaction control flags. */
    rpm_color_t tscolor {};	/*!< Transaction color. */
    rpm_color_t prefcolor {};	/*!< Transaction preferred color. */
    fingerPrintCache fpc {};
};

/**
 * Destroy available item index.
 * @param al		available list
 */
static void rpmalFreeIndex(rpmal al)
{
    delete al->providesHash;
    delete al->obsoletesHash;
    delete al->fileHash;
    al->fpc = fpCacheFree(al->fpc);
}

rpmal rpmalCreate(rpmts ts, int delta)
{
    rpmal al = new rpmal_s;

    al->pool = rpmstrPoolLink(rpmtsPool(ts));
    al->delta = delta;
    al->size = 0;
    al->alloced = al->delta;
    al->list = (availablePackage)xmalloc(sizeof(*al->list) * al->alloced);

    al->providesHash = NULL;
    al->obsoletesHash = NULL;
    al->tsflags = rpmtsFlags(ts);
    al->tscolor = rpmtsColor(ts);
    al->prefcolor = rpmtsPrefColor(ts);

    return al;
}

rpmal rpmalFree(rpmal al)
{
    availablePackage alp;
    int i;

    if (al == NULL)
	return NULL;

    if ((alp = al->list) != NULL)
    for (i = 0; i < al->size; i++, alp++) {
	alp->obsoletes = rpmdsFree(alp->obsoletes);
	alp->provides = rpmdsFree(alp->provides);
	alp->fi = rpmfilesFree(alp->fi);
    }
    al->pool = rpmstrPoolFree(al->pool);
    al->list = _free(al->list);
    al->alloced = 0;

    rpmalFreeIndex(al);
    delete al;
    return NULL;
}

void rpmalDel(rpmal al, rpmte p)
{
    availablePackage alp;
    rpmalNum pkgNum;

    if (al == NULL || al->list == NULL)
	return;		/* XXX can't happen */

    // XXX use a search for self provide
    for (pkgNum=0; pkgNum<al->size; pkgNum++) {
	if (al->list[pkgNum].p == p) {
	    break;
	}
    }
    if (pkgNum == al->size ) return; // Not found!

    alp = al->list + pkgNum;
    // do not actually delete, just set p to NULL
    // and later filter that out of the results
    alp->p = NULL;
}

static void rpmalAddFiles(rpmal al, rpmalNum pkgNum, rpmfiles fi)
{
    struct availableIndexFileEntry_s fileEntry;
    int fc = rpmfilesFC(fi);
    rpm_color_t ficolor;
    int skipdoc = (al->tsflags & RPMTRANS_FLAG_NODOCS);
    int skipconf = (al->tsflags & RPMTRANS_FLAG_NOCONFIGS);

    fileEntry.pkgNum = pkgNum;

    for (int i = 0; i < fc; i++) {
	/* Ignore colored provides not in our rainbow. */
        ficolor = rpmfilesFColor(fi, i);
        if (al->tscolor && ficolor && !(al->tscolor & ficolor))
            continue;

	/* Ignore files that wont be installed */
	if (skipdoc && (rpmfilesFFlags(fi, i) & RPMFILE_DOC))
	    continue;
	if (skipconf && (rpmfilesFFlags(fi, i) & RPMFILE_CONFIG))
	    continue;

	fileEntry.dirName = rpmfilesDNId(fi, rpmfilesDI(fi, i));
	fileEntry.entryIx = i;

	al->fileHash->insert({rpmfilesBNId(fi, i), fileEntry});
    }
}

static void rpmalAddProvides(rpmal al, rpmalNum pkgNum, rpmds provides)
{
    struct availableIndexEntry_s indexEntry;
    rpm_color_t dscolor;
    int skipconf = (al->tsflags & RPMTRANS_FLAG_NOCONFIGS);
    int dc = rpmdsCount(provides);

    indexEntry.pkgNum = pkgNum;

    for (int i = 0; i < dc; i++) {
        /* Ignore colored provides not in our rainbow. */
        dscolor = rpmdsColorIndex(provides, i);
        if (al->tscolor && dscolor && !(al->tscolor & dscolor))
            continue;

	/* Ignore config() provides if the files wont be installed */
	if (skipconf & (rpmdsFlagsIndex(provides, i) & RPMSENSE_CONFIG))
	    continue;

	indexEntry.entryIx = i;;
	al->providesHash->insert({rpmdsNIdIndex(provides, i), indexEntry});
    }
}

static void rpmalAddObsoletes(rpmal al, rpmalNum pkgNum, rpmds obsoletes)
{
    struct availableIndexEntry_s indexEntry;
    rpm_color_t dscolor;
    int dc = rpmdsCount(obsoletes);

    indexEntry.pkgNum = pkgNum;

    for (int i = 0; i < dc; i++) {
	/* Obsoletes shouldn't be colored but just in case... */
        dscolor = rpmdsColorIndex(obsoletes, i);
        if (al->tscolor && dscolor && !(al->tscolor & dscolor))
            continue;

	indexEntry.entryIx = i;;
	al->obsoletesHash->insert({rpmdsNIdIndex(obsoletes, i), indexEntry});
    }
}

void rpmalAdd(rpmal al, rpmte p)
{
    rpmalNum pkgNum;
    availablePackage alp;

    /* Source packages don't provide anything to depsolving */
    if (rpmteIsSource(p))
	return;

    if (al->size == al->alloced) {
	al->alloced += al->delta;
	al->list = xrealloc(al->list, sizeof(*al->list) * al->alloced);
    }
    pkgNum = al->size++;

    alp = al->list + pkgNum;

    alp->p = p;

    alp->provides = rpmdsLink(rpmteDS(p, RPMTAG_PROVIDENAME));
    alp->obsoletes = rpmdsLink(rpmteDS(p, RPMTAG_OBSOLETENAME));
    alp->fi = rpmteFiles(p);

    /* Try to be lazy as delayed hash creation is cheaper */
    if (al->providesHash != NULL)
	rpmalAddProvides(al, pkgNum, alp->provides);
    if (al->obsoletesHash != NULL)
	rpmalAddObsoletes(al, pkgNum, alp->obsoletes);
    if (al->fileHash != NULL)
	rpmalAddFiles(al, pkgNum, alp->fi);
}

static void rpmalMakeFileIndex(rpmal al)
{
    availablePackage alp;
    int i, fileCnt = 0;

    al->fileHash = new rpmalFileHash;

    for (i = 0; i < al->size; i++) {
	alp = al->list + i;
	if (alp->fi != NULL)
	    fileCnt += rpmfilesFC(alp->fi);
    }
    for (i = 0; i < al->size; i++) {
	alp = al->list + i;
	rpmalAddFiles(al, i, alp->fi);
    }
}

static void rpmalMakeProvidesIndex(rpmal al)
{
    availablePackage alp;
    int i, providesCnt = 0;

    for (i = 0; i < al->size; i++) {
	alp = al->list + i;
	providesCnt += rpmdsCount(alp->provides);
    }

    al->providesHash = new rpmalDepHash;
    for (i = 0; i < al->size; i++) {
	alp = al->list + i;
	rpmalAddProvides(al, i, alp->provides);
    }
}

static void rpmalMakeObsoletesIndex(rpmal al)
{
    availablePackage alp;
    int i, obsoletesCnt = 0;

    for (i = 0; i < al->size; i++) {
	alp = al->list + i;
	obsoletesCnt += rpmdsCount(alp->obsoletes);
    }

    al->obsoletesHash = new rpmalDepHash;
    for (i = 0; i < al->size; i++) {
	alp = al->list + i;
	rpmalAddObsoletes(al, i, alp->obsoletes);
    }
}

rpmte * rpmalAllObsoletes(rpmal al, rpmds ds)
{
    rpmte * ret = NULL;
    rpmsid nameId;

    if (al == NULL || ds == NULL || (nameId = rpmdsNId(ds)) == 0)
	return ret;

    if (al->obsoletesHash == NULL)
	rpmalMakeObsoletesIndex(al);

    auto result = al->obsoletesHash->equal_range(nameId);
    if (result.first != result.second) {
	availablePackage alp;
	int rc, found = 0;
	int resultCnt = std::distance(result.first, result.second);

	ret = (rpmte *)xmalloc((resultCnt+1) * sizeof(*ret));

	for (auto match = result.first; match != result.second; ++match) {
	    alp = al->list + match->second.pkgNum;
	    if (alp->p == NULL) // deleted
		continue;

	    rc = rpmdsCompareIndex(alp->obsoletes, match->second.entryIx,
				   ds, rpmdsIx(ds));

	    if (rc) {
		rpmdsNotify(ds, "(added obsolete)", 0);
		ret[found] = alp->p;
		found++;
	    }
	}

	if (found)
	    ret[found] = NULL;
	else
	    ret = _free(ret);
    }

    return ret;
}

static rpmte * rpmalAllFileSatisfiesDepend(const rpmal al, const char *fileName, const rpmds filterds)
{
    const char *slash; 
    rpmte * ret = NULL;

    if (al == NULL || fileName == NULL || *fileName != '/')
	return NULL;

    /* Split path into dirname and basename components for lookup */
    if ((slash = strrchr(fileName, '/')) != NULL) {
	size_t bnStart = (slash - fileName) + 1;
	rpmsid baseName;

	if (al->fileHash == NULL)
	    rpmalMakeFileIndex(al);

	baseName = rpmstrPoolId(al->pool, fileName + bnStart, 0);
	if (!baseName)
	    return NULL;	/* no match possible */

	auto result = al->fileHash->equal_range(baseName);
	if (result.first != result.second) {
	    int resultCnt = std::distance(result.first, result.second);
	    int found = 0;
	    ret = (rpmte *)xmalloc((resultCnt+1) * sizeof(*ret));
	    fingerPrint * fp = NULL;
	    rpmsid dirName = rpmstrPoolIdn(al->pool, fileName, bnStart, 1);

	    for (auto match = result.first; match != result.second; ++match) {
		availablePackage alp = al->list + match->second.pkgNum;
		if (alp->p == NULL) /* deleted */
		    continue;
		/* ignore self-conflicts/obsoletes */
		if (filterds && rpmteDS(alp->p, rpmdsTagN(filterds)) == filterds)
		    continue;
		if (match->second.dirName != dirName) {
		    /* if the directory is different check the fingerprints */
		    if (!al->fpc)
			al->fpc = fpCacheCreate(1001, al->pool);
		    if (!fp)
			fpLookupId(al->fpc, dirName, baseName, &fp);
		    if (!fpLookupEqualsId(al->fpc, fp, match->second.dirName, baseName))
			continue;
		}
		ret[found] = alp->p;
		found++;
	    }
	    _free(fp);
	    ret[found] = NULL;
	}
    }

    return ret;
}

rpmte * rpmalAllSatisfiesDepend(const rpmal al, const rpmds ds)
{
    rpmte * ret = NULL;
    int ix, found = 0;
    rpmsid nameId;
    const char *name;
    int obsolete;
    rpmTagVal dtag;
    rpmds filterds = NULL;

    availablePackage alp;
    int rc;

    if (al == NULL || ds == NULL || (nameId = rpmdsNId(ds)) == 0)
	return ret;

    dtag = rpmdsTagN(ds);
    obsolete = (dtag == RPMTAG_OBSOLETENAME);
    if (dtag == RPMTAG_OBSOLETENAME || dtag == RPMTAG_CONFLICTNAME)
	filterds = ds;
    name = rpmstrPoolStr(al->pool, nameId);
    if (!obsolete && *name == '/') {
	/* First, look for files "contained" in package ... */
	ret = rpmalAllFileSatisfiesDepend(al, name, filterds);
	if (ret != NULL && *ret != NULL) {
	    rpmdsNotify(ds, "(added files)", 0);
	    return ret;
	}
	/* ... then, look for files "provided" by package. */
	ret = _free(ret);
    }

    if (al->providesHash == NULL)
	rpmalMakeProvidesIndex(al);

    auto result = al->providesHash->equal_range(nameId);

    if (result.first == result.second) return NULL;

    int resultCnt = std::distance(result.first, result.second);
    ret = (rpmte *)xmalloc((resultCnt+1) * sizeof(*ret));

    for (auto match = result.first; match != result.second; ++match) {
	alp = al->list + match->second.pkgNum;
	if (alp->p == NULL) /* deleted */
	    continue;
	/* ignore self-conflicts/obsoletes */
	if (filterds && rpmteDS(alp->p, rpmdsTagN(filterds)) == filterds)
	    continue;
	ix = match->second.entryIx;

	if (obsolete) {
	    /* Obsoletes are on package NEVR only */
	    rpmds thisds;
	    if (!rstreq(rpmdsNIndex(alp->provides, ix), rpmteN(alp->p)))
		continue;
	    thisds = rpmteDS(alp->p, RPMTAG_NAME);
	    rc = rpmdsCompareIndex(thisds, rpmdsIx(thisds), ds, rpmdsIx(ds));
	} else {
	    rc = rpmdsCompareIndex(alp->provides, ix, ds, rpmdsIx(ds));
	}

	if (rc)
	    ret[found++] = alp->p;
    }

    if (found) {
	rpmdsNotify(ds, "(added provide)", 0);
	ret[found] = NULL;
    } else {
	ret = _free(ret);
    }

    return ret;
}

rpmte
rpmalSatisfiesDepend(const rpmal al, const rpmte te, const rpmds ds)
{
    rpmte *providers = rpmalAllSatisfiesDepend(al, ds);
    rpmte best = NULL;
    int bestscore = 0;

    if (providers) {
	rpm_color_t dscolor = rpmdsColor(ds);
	for (rpmte *p = providers; *p; p++) {
	    int score = 0;

	    /*
	     * For colored dependencies, prefer a matching colored provider.
	     * Otherwise prefer provider of ts preferred color.
	     */
	    if (al->tscolor) {
		rpm_color_t tecolor = rpmteColor(*p);
		if (dscolor) {
		    if (dscolor == tecolor) score += 2;
		} else if (al->prefcolor) {
		    if (al->prefcolor == tecolor) score += 2;
		}
	    }

	    /* Being self-provided is a bonus */
	    if (*p == te)
		score += 1;

	    if (score > bestscore) {
		bestscore = score;
		best = *p;
	    }
	}
	/* if not decided by now, just pick first match */
	if (!best) best = providers[0];
	free(providers);
    }
    return best;
}

unsigned int
rpmalLookupTE(const rpmal al, const rpmte te)
{
    rpmalNum pkgNum;
    for (pkgNum=0; pkgNum < al->size; pkgNum++)
	if (al->list[pkgNum].p == te)
	    break;
    return pkgNum < al->size ? pkgNum : (unsigned int)-1;
}

