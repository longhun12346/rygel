#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import configparser
import csv
import io
import sys
import zipfile

# -------------------------------------------------------------------------
# Parse
# -------------------------------------------------------------------------

def process_zip_file(zip_filename):
    build = None
    date = None
    prices = {
        'Public': {},
        'Private': {}
    }

    with zipfile.ZipFile(zip_filename) as zip:
        for info in zip.infolist():
            new_build = f'{info.date_time[0]:04}-{info.date_time[1]:02}-{info.date_time[2]:02}'
            if build is None or new_build > build:
                build = new_build

            with zip.open(info.filename, 'r') as csv_file:
                csv_file = io.TextIOWrapper(csv_file, encoding = 'ISO-8859-1')

                if info.filename.startswith('ghs_pub'):
                    prices['Public']['GHS'], date = extract_ghs_prices(csv_file)
                elif info.filename.startswith('ghs_pri'):
                    prices['Private']['GHS'], date = extract_ghs_prices(csv_file)
                elif info.filename.startswith('sup_pub'):
                    prices['Public']['Supplements'] = extract_supplements_prices(csv_file)
                elif info.filename.startswith('sup_pri'):
                    prices['Private']['Supplements'] = extract_supplements_prices(csv_file)

    return date, build, prices

def extract_ghs_prices(csv_file):
    reader = csv.DictReader(csv_file, delimiter = ';')
    date = None
    ghs = {}
    for row in reader:
        date = row['DATE-EFFET']
        ghs_info = {'PriceCents': parse_price_cents(row['GHS-PRI'])}
        if int(row['SEU-BAS']):
            ghs_info['ExbTreshold'] = int(row['SEU-BAS'])
            ghs_info['ExbCents'] = parse_price_cents(row['EXB-JOURNALIER'])
            if not ghs_info['ExbCents']:
                ghs_info['ExbCents'] = parse_price_cents(row['EXB-FORFAIT'])
                ghs_info['ExbType'] = 'Once'
        if int(row['SEU-HAU']):
            ghs_info['ExhTreshold'] = int(row['SEU-HAU']) + 1
            ghs_info['ExhCents'] = parse_price_cents(row['EXH-PRI'])
        ghs[int(row['GHS-NRO'])] = ghs_info
    date = '-'.join(reversed(date.split('/')))
    return ghs, date

def extract_supplements_prices(csv_file):
    reader = csv.DictReader(csv_file, delimiter = ';')
    supplements_dict = {}
    for row in reader:
        supplements_dict[row['CODE'].upper()] = parse_price_cents(row['TARIF'])
    return supplements_dict

def parse_price_cents(str):
    parts = str.split(',')
    if len(parts) > 1:
        return int(f"{parts[0]}{parts[1]:<02}")
    else:
        return int(parts[0]) * 100

# -------------------------------------------------------------------------
# Main
# -------------------------------------------------------------------------

def write_prices_ini(date, build, sector, ghs, supplements, file):
    print(f'Date = {date}', file = file)
    print(f'Build = {build}', file = file)
    print(f'Sector = {sector}', file = file)
    print('', file = file)

    cfg = configparser.ConfigParser()
    cfg.optionxform = str
    cfg.read_dict(ghs)
    cfg.read_dict({'Supplements': supplements})
    cfg.write(file)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description = 'Convert ATIH ZIP price files to INI files.')
    parser.add_argument('filenames', metavar = 'zip_filename', type = str, nargs = '+',
                        help = 'path to GHS price ZIP files')
    parser.add_argument('-D', '--destination', dest = 'destination', action = 'store',
                        required = True, help = 'destination directory')
    args = parser.parse_args()

    for zip_filename in args.filenames:
        date, build, prices = process_zip_file(zip_filename)
        prefix = date.replace('-', '')

        with open(f'{args.destination}/{prefix}_public.dpri', 'w') as ini_file:
            write_prices_ini(date, build, 'Public', prices['Public']['GHS'],
                             prices['Public']['Supplements'], ini_file)
        with open(f'{args.destination}/{prefix}_private.dpri', 'w') as ini_file:
            write_prices_ini(date, build, 'Private', prices['Private']['GHS'],
                             prices['Private']['Supplements'], ini_file)
