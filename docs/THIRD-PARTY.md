# Third-party notices

CyberSnapper bundles or depends on the following third-party materials. This
file is bundled with release artifacts alongside the ISC license in `LICENSE`.

## @ghostery/adblocker (worker)

The capture worker uses [@ghostery/adblocker](https://github.com/ghostery/adblocker)
to parse EasyList-style filter syntax. It is licensed under the
**Mozilla Public License 2.0**. A local copy of the license text is available
in `node_modules/@ghostery/adblocker/LICENSE` inside the source distribution.
CyberSnapper uses the library's rule-matching engine only; it does not use its
browser-extension routing wrappers.

## Community filter lists

Curated subscription lists are downloaded at runtime from their official
publishers and remain the property of their authors:

| Subscription | Publisher | License |
| --- | --- | --- |
| EasyList Cookie, EasyList, EasyPrivacy | [EasyList](https://easylist.to/) authors | Creative Commons Attribution-ShareAlike 3.0 Unported |
| uBlock Cookie Notices, uBlock Annoyances | [uAssets](https://github.com/uBlockOrigin/uAssets) contributors | GNU General Public License v3.0 |

Subscription metadata follows the official uBlock list catalog:
<https://github.com/gorhill/uBlock/blob/master/assets/assets.json>. Filter-list
licenses are documented by uBlock at
<https://github.com/gorhill/uBlock/wiki/Filter-list-licenses>.

Lists are cached locally after download; CyberSnapper never modifies or
redistributes list content. Custom rulesets authored by users belong to the
user.

## Other runtime dependencies

`playwright` (Apache-2.0), `sharp` (Apache-2.0), and the Qt 6 framework
(LGPL-3.0/GPL-3.0 commercial dual-license, linked dynamically) retain their
own licenses as distributed. Consult each package for full notices.
