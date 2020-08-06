# Copyright 2020 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7
inherit git-r3

DESCRIPTION="A rewrite of euses(1) for modern Gentoo-like systems."
HOMEPAGE="http://git.suugaku.co.uk/ash-euses/"
EGIT_REPO_URI="https://git.suugaku.co.uk/ash-euses/"

LICENSE="WTFPL-2"
SLOT="0"
KEYWORDS="~amd64 ~x86"

S=${WORKDIR}

src_install() {
	# cgit automatic archiving places the files in a subfolder.
	mkdir -p ${S}/${P}
	cd ${S}/${P}
	emake DESTDIR="${D}" install
	doman ${PN}.1
}

