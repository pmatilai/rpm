RPM-DEPENDENCIES(7)

# NAME
*rpm-dependencies* - RPM dependencies

# SYNOPSIS
_CAPABILITY_[*(*_MODIFIERS_*)*] [_COMP_OPER_ _VALUE_]

*(*_DEPENDENCY_ _OPER_ _DEPENDENCY_*)*

# DESCRIPTION
RPM uses _dependencies_ to describe various relations among packages.
Dependencies are used to express what capabilities are needed for
software to function, and constraints such as conflicting with another
package providing similar functionality. RPM also uses the dependency
information for calculating the right installation and erase ordering.

Dependencies can be either _file-backed_, such as the path of a packaged
file or a dependency generated from such a file, or _virtual_. Most
automatically generated dependencies are file-backed. All dependencies
added manually to a spec are virtual.

The four main dependency types are:

*Requires*
	Capabilities a package requires to function at all.

*Provides*
	Capabilities provided by a package (or the system).

*Conflicts*
	Capabilities a package conflicts with, typically packages
	with conflicting paths or otherwise conflicting functionality.

*Obsoletes*
	Packages obsoleted by a package. Used for replacing and
	renaming packages.

	Note: in contrast to all other dependency types, *Obsoletes*
	are matched against package *Name* rather than *Provides*
	capabilities.

Weak dependencies present a group of optional dependency types, derived
from the main dependency types. RPM only uses the weak dependency information
for ordering hints, they are mainly targeted at dependency solver programs.

*Recommends*
	Optional capabilities recommended by a package for the best user
	experience.

*Suggests*
	Optional capabilities that provide additional functionality for
	a package.

*Supplements*
	Reverse *Recommends*.

*Enhances*
	Reverse *Enhances*.

*OrderWithRequires*
	Optional capabilities for ordering calculations. IFF the capability
	is present in a transaction, calculate ordering as if it was a
	*Requires*.

Build dependencies only exist in the context of building a package
with *rpmbuild*(1). They are not carried into binary packages at all,
and are recorded as regular *Requires* and *Conflicts*
in source rpms.

*BuildRequires*

*BuildConflicts*

## Modifiers
Most dependency types accept additional modifiers to change where and
how exactly the dependency behaves.

*interp*
	Denotes a scriptlet interpreter dependency, usually added automatically
	by rpm. Used as a strong ordering hint for breaking dependency loops.

*meta* (since rpm >= 4.16)
	Denotes a "meta" dependency, which must not affect transaction
	ordering. Typical use-cases would be meta-packages and sub-package
	cross-dependencies whose purpose is just to ensure the sub-packages
	stay on common version.

*post*
	Denotes the dependency must be present right after the package is
	is installed, and is used a strong ordering hint to break possible
	dependency loops. A *post*-dependency is free to be removed
	once the install-transaction completes.

	Also relates to *%post* scriptlet execution.

*postun*
	Denotes the dependency must be present right after the package is
	is removed, and is used a strong ordering hint to break possible
	dependency loops.

	Also relates to *%postun* scriptlet execution.

*pre*
	Denotes the dependency must be present in before the package is
	is installed, and is used a strong ordering hint to break possible
	dependency loops. A pre-dependency is free to be removed
	once the install-transaction completes.

	Also relates to *%pre* scriptlet execution.

*preun*
	Denotes the dependency must be present in before the package is
	is removed, and is used a strong ordering hint to break possible
	dependency loops.

	Also relates to *%preun* scriptlet execution.

*posttrans*
	Denotes the dependency must be present at the end of transaction, ie
	cannot be removed during the transaction. As such, it does not affect
	transaction ordering. A posttrans-dependency is free to be removed
	after the the install-transaction completes.

	Also relates to *%posttrans* and *%postuntrans* scriptlet execution.

*verify*
	Relates to *%verify* scriptlet execution. As the *%verify* scriptlet
	is not executed during install/erase, this does not affect transaction
	ordering.

Multiple modifiers can be supplied separated by comma, as long as
they're not semantically contradictory: *meta* modifier contradicts any
ordered modifier, eg *meta* and *verify* can be combined, and *pre* and
*verify* can be combined, but *pre* and *meta* can not.

As noted above, install-time only dependencies (*pretrans*,
*pre*, *post*, *posttrans* or combination of them) can be removed after the
installation transaction completes if there are no other dependencies
to prevent that. This is a common source of confusion.

# BOOLEAN DEPENDENCIES
RPM can process boolean expressions in
Requires, Recommends, Suggests, Supplements, Enhances and Conflicts type
dependencies. Boolean expressions are always enclosed with parenthesis.
They are built out of two or more dependencies (operands) and boolean
operators:

*(*_DEPENDENCY_ _OPER_ _DEPENDENCY_ ...*)*

Operands can be boolean expressions themselves. They also need to be surrounded
by parenthesis. *and* and *or* operators may be chained together repeating the
same operator with only one set of surrounding parenthesis.

The semantics of the dependencies are unchanged, but instead of checking for
one match, all names are checked and the boolean value of a match is then
aggregated over the boolean operators. For a conflict to not prevent an install
the result has to be _false_, for everything else it has to be _true_.

## Boolean operators
*and*
	Requires all operands to be fulfilled for the term to be True.

	Example:
	```
	Conflicts: (pkgA and pkgB)
	```
*or*
	Requires one of the operands to be fulfilled.

	Example:
	```
	Requires: (pkgA >= 3.2 or pkgB)
	```
*if*
	Requires the first operand to be fulfilled if the second is
	(reverse implication).

	Example:
	```
	Recommends: (myPkg-langCZ if langsupportCZ)
	```
*if else*
	Same as *if*, but require the third operand to be fulfilled if the
	second is not.

	Example:
	```
	Requires: (myPkg-backend-mariaDB if mariaDB else sqlite)
	```
*with*
	Requires all operands to be fulfilled by the same package for the
	term to be True.

	Example:
	```
	Requires: (pkgA-foo with pkgA-bar)
	```
*without*
	Requires a single package that satisfies the first operand but not
	the second (set subtraction).

	Example:
	```
	Requires: (pkgA-foo without pkgA-bar)
	```
*unless*
	Requires the first operand to be fulfilled if the second is not
	(reverse negative implication).

	Example:
	```
	Conflicts: (myPkg-driverA unless driverB)
	```
*unless else*
	Same as *unless* but requires the third operand to be fulfilled if
	the second is.

	Example:
	```
	Conflicts: (myPkg-backend-SDL1 unless myPkg-backend-SDL2 else SDL2)
	```

Note: the *if* operators cannot be used in the same context with *or*, and
the *unless* operators cannot be used in the same context with *and*.

Added: 4.14

# SPECIAL DEPENDENCIES

## Implicit file provides
All packages implicitly provide the pathnames of the contained files.
Packages can thus depend on an arbitrary path, such as _/bin/sh_ to
request the standard shell interpreter without specifying a package name
explicitly.

These differ from manually added provides of equal values in two ways.
Implicit file provides are
- not visible in a packages provides dependencies (eg. *--provides* query)
- subject to package install policies such as *--nodocs*:
  files that are not (being) installed are not provided

## Package NEVR provides
All packages explicitly provide their own _NEVR_:

_NAME_ = [_EPOCH_:]_VERSION_-_RELEASE_

Additionally, all architecture dependent packages explicitly provide an _ISA_
dependency which can be used for requiring an specific architecture family
among multiple providers:

_NAME_*(*_ISA_*)* = [_EPOCH_:]_VERSION_-_RELEASE_

The *%{\_isa}* macro can be used to populate the _ISA_ value in an
*rpm-spec*(5). ISA consists of two parts separated with a dash (*-*):
the name of the architecture family in *%\_\_isa\_name* and its
bitness in *%\_\_isa\_bits*.

## System provides
Most dependencies are provided by other packages, but there are some
exceptions where the running system is used for provide matching.
As a general rule, capabilities provided by the system are free of
ordering constraints.

System users and groups usually originate from other installed packages,
but they can also originate from a central name service system.
To allow packaged content to reference such users and groups,
*user()* and *group()* requires are first matched against the system
*nss*(5), and only then other packages.

## rpmlib() dependencies
RPM uses special *rpmlib()* dependencies to track feature compatibility.
Packages utilizing features that are not supported in all RPM versions
are automatically flagged with an *rpmlib(*_CAPABILITY_*)*
requires-dependency by *rpmbuild*(1). Some of these are
compile-time options when building RPM, others are RPM version-dependent.

*rpmlib()* requires cannot be satisfied by other packages.
Instead, they dependencies are matched against the RPM process performing
the installation. The *rpmlib()* features supported by the system
RPM can be inspected by running *rpm --showrc*.

# EXAMPLES

## Example #. ISA dependency
Declare a dependency on the same architecture family of the package:

```
Requires: libbar-devel%{?\_isa} >= 2.2
```

When built for the *x86_64* architecture, this becomes
```
Requires: libbar-devel(x86-64) >= 2.2
```

When built for the *x86* family (i386, i586, i686 etc.) it becomes
```
Requires: libbar-devel(x86-32) >= 2.2
```

## Example #. Complex ISA dependency
Require a 32bit version of a package along with the 64bit version:

```
Requires: foo%{\_isa}
%if %{\_\_isa\_bits} == 64
Requires: foo(%{\_\_isa\_name}-32)
%endif
```

## Example #. Boolean dependencies
```
Requires: (pkgA or pkgB or pkgC)

Requires: (pkgA or (pkgB and pkgC))

Supplements: (foo and (lang-support-cz or lang-support-all))

Requires: ((pkgA with capB) or (pkgB without capA))

Supplements: ((driverA and driverA-tools) unless driverB)

Recommends: ((myPkg-langCZ and (font1-langCZ or font2-langCZ)) if la
```

# SEE ALSO
*rpm*(8) *rpm-common*(8) *rpm-dependency-generators*(7) *rpm-sysusers*(7)

*http://www.rpm.org/*
