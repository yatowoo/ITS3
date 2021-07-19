#!/bin/env python3
 
import os, json, copy, argparse
import config_thr # User libs

KEY_VALUE_FORMATTER = '%-16s = %s'
def print_section(headers, out=None):
  if(out==None):
    output = print
  else:
    output = lambda str: out.write(str + '\n')
  for name in headers:
    output('[%s]' % name)
    for k, v in SETUP_DB[name].items():
      output(KEY_VALUE_FORMATTER % (k, v))
    output('')

def print_conf(out=None):
  print_section(['RunControl', 'Producer.POWER', 'Producer.PTH'], out)
  # Chip config.
  chipName = []
  for chipID in SETUP_DB['general']['Setup']:
    chipConf = SETUP_DB['CHIPS'][chipID]
    header = chipConf['name']
    chipName.append(header)
    confData = SETUP_DB[header] = copy.deepcopy(SETUP_DB['general']['ALPIDE'])
    for k in chipConf.keys():
      if(k == 'name'):
        continue
      confData[k] = chipConf[k]
  print_section(chipName, out)
  print_section(['DataCollector.dc'], out)

if __name__ == '__main__':
  parser=argparse.ArgumentParser(description='Configuration file generator',formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument('--setup', '-s',default='Setup.json', help='Setup file, see example.')
  args=parser.parse_args()
  # Init the setup info.
  with open(args.setup) as f:
    SETUP_DB = json.load(f)
  # Get VCASN settings from Cubic-spline fitting
  config_thr.InitScanData(SETUP_DB['general']['thr_scan'])
  config_thr.ThresholdForConfig(SETUP_DB['general']['thr_conf'])
  for ithr in SETUP_DB['general']['ithr_conf']:
    for i, thr in enumerate(SETUP_DB['general']['thr_conf']):
      confDir = f'{SETUP_DB["general"]["title"]}_ithr_{ithr}/'
      try:
        os.mkdir(confDir)
      except FileExistsError:
        pass
      with open(f'{confDir}/{SETUP_DB["general"]["title"]}_ithr_{ithr}-{thr}.conf','w') as f:
        for chip in config_thr.THR_DATA.keys():
          vcasn = round(config_thr.THR_DATA[chip][ithr]['vcasn_config'][i])
          SETUP_DB['CHIPS'][chip]['ITHR'] = ithr
          SETUP_DB['CHIPS'][chip]['VCASN'] = vcasn
          SETUP_DB['CHIPS'][chip]['VCASN2'] = vcasn + 12
        print_conf(f)

