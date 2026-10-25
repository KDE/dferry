// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifdef _WIN32

#ifdef dfer_EXPORTS
#define DFERRY_EXPORT __declspec(dllexport)
#else
#define DFERRY_EXPORT __declspec(dllimport)
#endif

#ifdef dferclient_EXPORTS
#define DFERRYCLIENT_EXPORT __declspec(dllexport)
#else
#define DFERRYCLIENT_EXPORT __declspec(dllimport)
#endif

#else

#ifdef dfer_EXPORTS
#define DFERRY_EXPORT __attribute__ ((visibility ("default")))
#else
#define DFERRY_EXPORT __attribute__ ((visibility ("default")))
#endif

#ifdef dferclient_EXPORTS
#define DFERRYCLIENT_EXPORT __attribute__ ((visibility ("default")))
#else
#define DFERRYCLIENT_EXPORT __attribute__ ((visibility ("default")))
#endif

#endif // _WIN32
