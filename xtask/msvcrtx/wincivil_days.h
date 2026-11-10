/* SPDX-License-Identifier: GPL-2.0-or-later
 * Gregorian days since Unix epoch, shared by FILETIME and date formatting. */
static i64 days_from_civil(i64 y, unsigned m, unsigned d) {
    y -= m <= 2; i64 era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (i64)doe - 719468;
}
