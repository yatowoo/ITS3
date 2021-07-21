#!/bin/env python3

import os, time, re, csv, json, subprocess
import argparse
from scipy import interpolate

# INPUT
parser=argparse.ArgumentParser(description='Run list generator',formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--data', '-d',default='/media/T71/SPSjuly2021/uITS3g1/Vbb0/', help='Data directory')
parser.add_argument('--run', '-r',default='/home/llautner/eudaq2/user/ITS3/misc/', help='Path for configuration files')
parser.add_argument('--nothr', default=False, action='store_true', help='Disable threshold extrapolation')
parser.add_argument('--setup', default='Setup.json', help='Setup file, contains configuration info.')
parser.add_argument('--eudaq', default='/home/llautner/eudaq2/', help='Use $EUDAQ/bin/euCliReader to get event number')
parser.add_argument('--log',default=False,action='store_true',help='Output short run list for eLog entry')
parser.add_argument('--debug','-v',default=False, action='store_true', help='Print debug info. (also skip event number)')
args=parser.parse_args()

RUN_DIR = args.run
DATA_DIR = args.data
with open(args.setup) as f:
  SETUP_DB = json.load(f)

# Result from Threshold scan
THR_FLAG = not args.nothr
if(THR_FLAG):
  import config_thr
  config_thr.InitScanData(SETUP_DB['general']['thr_scan'])

RUNLIST_CSV_FIELDS = ['RunNumber','Size','Config','Date','Time','VBB', 'Nevents']
# DAC of DUTs - ITHR, VCASN, THRe
DUT_LIST = []
for chip in SETUP_DB['general']['setup']:
  if(chip.startswith('DUT')):
    DUT_LIST.append(chip)
    RUNLIST_CSV_FIELDS += [f'ITHR_{chip}', f'VCASN_{chip}']
    if(THR_FLAG):
      RUNLIST_CSV_FIELDS.append(f'THRe_{chip}')

outputName = f'Runlist_{SETUP_DB["general"]["title"]}_{SETUP_DB["general"]["Vbb"]}V'
fOut = open(f'{outputName}.csv','w')
csvWriter = csv.DictWriter(fOut, fieldnames=RUNLIST_CSV_FIELDS,extrasaction='ignore')
csvWriter.writeheader()

# eLog entry
RUNLIST_ELOG_FIELDS = ['RunNumber','THRe','Config','End','Nevents','Size']
ELOG_FORMAT = '{x["RunNumber"]:s}|{x["THRe"]:^6.0f}|{x["Config"]:^35s}|{x["End"]:s}|{x["Nevents"]:6d}|{x["Size"]:s}|'
if(args.log):
  fLog = open(f'{outputName}.log','w')

# Parse .conf file
def ReadConf(confPath):
  confData = {}
  with open(confPath) as f:
    tmpHeader = ''
    headerRe = re.compile(r'\[(.*)\].*')
    varRe = re.compile('(\w+)[ ]+=[ ]+(\w+)')
    for l in f.readlines():
      if(headerRe.match(l) is not None):
        tmpHeader = headerRe.match(l).group(1)
        confData[tmpHeader] = {}
      elif(varRe.match(l) is not None):
        var = varRe.match(l).group(1)
        val = varRe.match(l).group(2)
        if(val.isdigit()):
          val = int(val)
        confData[tmpHeader][var] = val
      else: # Empty or commented lines only
        pass
  return confData
def GetFileSize(rawDataPath):
  return os.path.getsize(rawDataPath)
def GetNevents(rawDataPath):
  # From Bogdan
  cmd = [args.eudaq + '/bin/euCliReader',"-i",rawDataPath,"-e", "10000000"]
  result = subprocess.run(cmd, stdout=subprocess.PIPE)
  cfg = re.findall(r'\d+',result.stdout.decode('utf-8'))
  return int(cfg[0])

# Run list
RUN_NOW_TIME = time.time() - 60
for fileName in next(os.walk(DATA_DIR))[2]:
  if(not fileName.endswith('.raw')):
    continue
  filePath = DATA_DIR + '/' + fileName
  fileTime = os.path.getmtime(filePath)
  if(fileTime > RUN_NOW_TIME):
    continue
  runInfo = {}
  runInfo['RunNumber'] = fileName
  runInfo['Size'] = GetFileSize(filePath)
  if(args.debug):
      runInfo['Nevents'] = 300000
  else:
    runInfo['Nevents'] = GetNevents(filePath)
  timeStamp = fileName.split('_')[1]
  runInfo['Date'] = timeStamp[0:2] + '.' + timeStamp[2:4] + '.' + timeStamp[4:6]
  runInfo['Time'] = timeStamp[6:8] + ':' + timeStamp[8:10]
  runInfo['End'] = time.strftime('%H:%M', time.localtime(fileTime))
  runInfo['VBB'] = SETUP_DB['general']['Vbb']
  with open(filePath, errors='ignore') as f:
    if(args.debug):
      print(f'> Processing {filePath}')
    for i in range(20):
      l = f.readline()
      varMatch = re.match('Name[ ]+=[ ]+(.*)', l)
      if(varMatch is None):
        continue
      else:
        configFile = varMatch.group(1)
    runInfo['configPath'] = RUN_DIR + configFile
    runInfo['Config'] = os.path.basename(configFile)
    runInfo['confData'] = ReadConf(runInfo['configPath'])
    thrAvg = 0.
    for chip in DUT_LIST:
      dutConfig = runInfo['confData'] [SETUP_DB['CHIPS'][chip]['name']]
      runInfo[f'ITHR_{chip}'] = dutConfig['ITHR']
      runInfo[f'VCASN_{chip}'] = dutConfig['VCASN']
      if(THR_FLAG):
        fit = config_thr.THR_DATA[chip][dutConfig['ITHR']]['fit']
        fitThr = interpolate.splev(dutConfig['VCASN'], fit, der=0)
        runInfo[f'THRe_{chip}'] = round(10 * float(fitThr))
        thrAvg += runInfo[f'THRe_{chip}']
      else:
        thrAvg += 100.
    thrAvg /= len(DUT_LIST)
    runInfo['THRe'] = round(thrAvg)
    csvWriter.writerow(runInfo)
    if(args.debug):
      print(','.join([str(runInfo[k]) for k in RUNLIST_CSV_FIELDS]))
    if(args.log):
      fLog.write(ELOG_FORMAT.format(x=runInfo) + '\n')
# Output
print('[+] Run list generated --> ' + fOut.name)
fOut.close()
if(args.log):
  print('[+] Run list generated for eLog ---> ' + fLog.name)
  fLog.close()
# END
