#include <rpm/rpmlib.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmts.h>
#include <rpm/header.h>

#include <stdio.h>

void printtag(Header h, rpmTagVal tag)
{
    char *v = headerGetAsString(h, tag);
    printf("%u: %s\n", tag, v);
    free(v);
}

int main(int argc, char *argv[])
{
    rpmts ts = rpmtsCreate();
    rpmtd td = rpmtdNew();
    Header h = NULL;
    FD_t fd = NULL;
    int rc = EXIT_FAILURE;

    if (rpmReadConfigFiles(NULL, NULL))
	goto exit;

    fd = Fopen(argv[1], "r");
    if (fd == NULL)
	goto exit;
    if (rpmReadPackageFile(ts, fd, argv[1], &h))
	goto exit;

    printtag(h, RPMTAG_HDRID);
    printtag(h, RPMTAG_PAYLOADDIGEST);
    printtag(h, RPMTAG_PAYLOADDIGESTALT);
    printtag(h, RPMTAG_PAYLOADDIGESTALGO);
    printtag(h, RPMTAG_PKGID);
    printtag(h, RPMTAG_SOURCEPKGID);

    rc = 0;

exit:
    Fclose(fd);
    headerFree(h);
    rpmtdFree(td);
    rpmtsFree(ts);
    rpmFreeRpmrc();
    return rc;
}
