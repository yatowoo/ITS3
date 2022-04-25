#include "eudaq/StdEventConverter.hh"
#include "eudaq/RawEvent.hh"
#include <iostream>
#include <vector>
#include <cmath>

#include "TSystem.h"
#include "TFile.h"
#include "TH2.h"
#include "TROOT.h"

class CE65RawEvent2StdEventConverter:public eudaq::StdEventConverter{
public:
  static const int X_MX_SIZE = 64;
  static const int Y_MX_SIZE = 32;
public:
  bool Converting(eudaq::EventSPC rawev,eudaq::StdEventSP stdev,eudaq::ConfigSPC conf) const override;
private:
  // Configuration from conf file
  struct Config{
    bool flagInitialized;
    bool flagSimpleCut;
    bool flagMonitor;
    int  seedSNR;
    int  N_SUBMATRIX;
    int* subEdge;
    int* subThr;
    // calibration file
    TH2* hPedestal;
    TH2* hNoise;
  };
  static Config confLocal;

  void Dump(const std::vector<uint8_t> &data,size_t i) const;
  bool LoadCalibration(const std::string& path) const;
  void InitConfiguration() const;
  bool LoadConfiguration(eudaq::ConfigSPC conf) const;
  bool findSubEvent(eudaq::ConfigSPC conf) const;
};

// Definitions for static members
const int CE65RawEvent2StdEventConverter::X_MX_SIZE;
const int CE65RawEvent2StdEventConverter::Y_MX_SIZE;
CE65RawEvent2StdEventConverter::Config CE65RawEvent2StdEventConverter::confLocal = {false};

#define REGISTER_CONVERTER(name) namespace{auto dummy##name=eudaq::Factory<eudaq::StdEventConverter>::Register<CE65RawEvent2StdEventConverter>(eudaq::cstr2hash(#name));}
REGISTER_CONVERTER(CE65)
REGISTER_CONVERTER(CE65Raw)
REGISTER_CONVERTER(ce65_producer)

/**
 * @brief Skip sub-events from other producers
 * 
 * @param conf 
 * @return true 
 * @return false - other sub-event (only corry?)
 */
bool CE65RawEvent2StdEventConverter::findSubEvent(eudaq::ConfigSPC conf) const{
  if(!conf) return true;

  if (conf->Has("identifier")){
    std::string id = conf->Get("identifier","");
    if(id.find("CE65_") == std::string::npos) return false; // other sub-event (call from corry)
  }
  return true;
}

/**
 * @brief Load pedestal and noise map from ROOT file
 * 
 * @param path 
 * @return true  - file found and loaded
 * @return false - fail
 * 
 * ROOT file format
 * - hPedestalpl1/TH2D
 * - hnoisepl1/TH2D
 */
bool CE65RawEvent2StdEventConverter::LoadCalibration(const std::string& path) const {
  std::cout << " > calibration_file = " << path << std::endl;

  // Check file accessability
  if(gSystem->AccessPathName(path.c_str())){
    std::cout << "[+] CE65 - Calibration file not found - " << path << std::endl;
    return false;
  }
  try{
    // Multi-threading protection
    ROOT::EnableImplicitMT();
    ROOT::EnableThreadSafety();
    TFile* f = TFile::Open(path.c_str(), "READ");
    std::cout << "[+] CE65 - Input file : " << path.c_str() << std::endl;// DEBUG
    confLocal.hPedestal = (TH2*)( f->Get("hPedestalpl1")->Clone("hPedestal"));
    confLocal.hNoise = (TH2*)( f->Get("hnoisepl1")->Clone("hNoise"));
    confLocal.hPedestal->SetDirectory(nullptr);
    confLocal.hNoise->SetDirectory(nullptr);
    std::cout << "> Pedestal & Noise map loaded."<< std::endl;// DEBUG
    f->Close();
  }catch(std::exception& e){
    std::cout << "[+] CE65 - FAIL to read calibration file- " << path << std::endl;
    return false;
  }
  std::cout << "> Calibrated converter - Signal/Noise ratio mode"<< std::endl;// DEBUG
  return true;
}

void CE65RawEvent2StdEventConverter::InitConfiguration() const{
  confLocal.flagInitialized = true;
  confLocal.flagSimpleCut = true;
  confLocal.flagMonitor = false;

  confLocal.seedSNR = 10;
  confLocal.hPedestal = nullptr;
  confLocal.hNoise = nullptr;

  confLocal.N_SUBMATRIX = 3;
  confLocal.subEdge = new int[confLocal.N_SUBMATRIX];
  confLocal.subThr = new int[confLocal.N_SUBMATRIX];
  confLocal.subEdge[0] = 21;
  confLocal.subEdge[1] = 42;
  confLocal.subEdge[2] = 64;
  confLocal.subThr[0] = 1500;
  confLocal.subThr[1] = 1800;
  confLocal.subThr[2] = 500;
}
/**
 * @brief Read configuration file
 * 
 * @param conf 
 * @return true  - Success
 * @return false - Fail to load and/or resolve
 * 
 * .conf file example (TOML):
 * 
 * eudaq2::StdEventMonitor
 * - [CE65]
 * - submatrix_n = 3
 * - submatrix_edge = 21, 42, 64
 * - submatrix_threshold = 1500, 1800, 500
 * - calibration_file = ce65_debug.root
 * - seed_threshold_snr = 10
 * 
 * corryvreckan::EvenLoaderEUDAQ2
 * - identifier = CE65_4
 * - calibration_file = ce65_debug.root
 */
bool CE65RawEvent2StdEventConverter::LoadConfiguration(eudaq::ConfigSPC conf) const{
  // Initialization
  if(confLocal.flagInitialized) return true;
  // Default value
  InitConfiguration();
  
  // No conf - use basic converter
  if(!conf){
    std::cout << "[-] CE65 default configuration - digital mode (SimpleCut)" << std::endl;
    return false;
  }

  std::cout << "[-] INFO - Load configuration for CE65" << std::endl;
  // eudaq2-StdEventMonitor
  if(conf->HasSection("CE65")){
    std::cout << " > Template : eudaq2-StdEventMonitor" << std::endl;
    conf->SetSection("CE65");
    confLocal.flagMonitor = true;

    // Load - Resolve string
    if(conf->Has("calibration_file") && 
      LoadCalibration(conf->Get("calibration_file", "ce65_debug.root"))){
      confLocal.flagSimpleCut = false;
      confLocal.seedSNR = conf->Get("seed_threshold_snr", confLocal.seedSNR);
      std::cout << " > seed_threshold_snr = " << confLocal.seedSNR << std::endl;
      return true;
    }

    confLocal.N_SUBMATRIX = conf->Get("submatrix_n", confLocal.N_SUBMATRIX);
    std::cout << " > submatrix_n = " << confLocal.N_SUBMATRIX << std::endl;
    if(confLocal.subEdge){
      delete confLocal.subEdge;
      confLocal.subEdge = nullptr;
    }
    confLocal.subEdge = new int[confLocal.N_SUBMATRIX];

    if(confLocal.subEdge){
      delete confLocal.subEdge;
      confLocal.subEdge = nullptr;
    }
    confLocal.subThr = new int[confLocal.N_SUBMATRIX];

    std::cout << " > submatrix_edge = ";
    std::istringstream fssEdge(conf->Get("submatrix_edge", " 21, 42, 64"));
    for(int i =0;i<confLocal.N_SUBMATRIX;i++){
      std::string sEdge;
      std::getline(fssEdge, sEdge, ',');
      confLocal.subEdge[i] = std::stoi(sEdge);
      std::cout << confLocal.subEdge[i] << ", ";
    }std::cout << std::endl;
    
    std::cout << " > submatrix_threshold = ";
    std::istringstream fssThr(conf->Get("submatrix_threshold", " 1500, 1800, 500"));
    for(int i =0;i<confLocal.N_SUBMATRIX;i++){
      std::string sThr;
      std::getline(fssThr, sThr, ',');
      confLocal.subThr[i] = std::stoi(sThr);
      std::cout << confLocal.subThr[i] << ", ";
    }std::cout << std::endl;

  // corryvreckan::EventLoaderEUDAQ2
  }else if (conf->Has("identifier")){
    std::cout << " > Template : corryvreckan::EventLoaderEUDAQ2" << std::endl;
    confLocal.flagSimpleCut = !LoadCalibration(conf->Get("calibration_file", "ce65_debug.root"));

  // Unknown case
  }else{
    std::cout << "[+] WARNING - Unknown .conf file - " << __func__ << std::endl;
    std::cout << "> TRY - default converter in digital mode (SimpleCut)" << std::endl;
    return false;
  }

  if(confLocal.flagSimpleCut){
    std::cout << "> Default converter - digital mode (SimpleCut)" << std::endl;
  }

  return true;
}

/** CE65Producer from https://gitlab.cern.ch/alice-its3-wp3/ce65_daq
Raw event name in data sender: CE65Raw
Raw event structure: decoded into pixel frames, uint16 -> uint8 *2
Param: configuration scheme, see LoadConfiguration(...)
**/
bool CE65RawEvent2StdEventConverter::Converting(eudaq::EventSPC in,eudaq::StdEventSP out,eudaq::ConfigSPC conf) const{
  
  if(!findSubEvent(conf)) return false;
  
  // Read configuration file
    // TODO: more than 1 plane(CE65), by sub-event name
  LoadConfiguration(conf);

  auto rawev=std::dynamic_pointer_cast<const eudaq::RawEvent>(in);
  
  eudaq::StandardPlane plane(rawev->GetDeviceN(),"ITS3DAQ","CE65");
  plane.SetSizeZS(X_MX_SIZE, Y_MX_SIZE,0,1);
    // TODO: other information - event no., timestamp, trigger, ...
  
  // Frame <-> Block (from Producer)
  std::vector<uint8_t> data=rawev->GetBlock(0); // 1st frame
  std::vector<uint8_t> dataLast = rawev->GetBlock(rawev->NumBlocks() - 1); // Last frame
  size_t n=data.size();
  for(int ix=0; ix < X_MX_SIZE; ix++){
    int thr = 0;
    if(confLocal.flagSimpleCut){
      for(int iSub = 0; iSub < confLocal.N_SUBMATRIX; iSub++){
        if(ix < confLocal.subEdge[iSub]){
          thr = confLocal.subThr[iSub];
          break;
        }}}// Set threshold for this sub-matrix
    for(int iy=0; iy < Y_MX_SIZE; iy++){
      int iPixel = iy + ix * Y_MX_SIZE;
      // raw amp: uint8 *2 -> uint16
      uint8_t byteB = data[iPixel * sizeof(short) + 1];
      uint8_t byteA = data[iPixel * sizeof(short)];
      short valueFirst = short((byteB<<8)+byteA);
      byteB = dataLast[iPixel * sizeof(short) + 1];
      byteA = dataLast[iPixel * sizeof(short)];
      short valueLast = short((byteB<<8)+byteA);
      float raw = float(valueLast - valueFirst); // CDS
      if(!confLocal.flagSimpleCut){
        float valNoise = confLocal.hNoise->GetBinContent(ix+1, iy+1);
        float valPedestal = confLocal.hPedestal->GetBinContent(ix+1, iy+1);
        // online monitor - eudaq2
        if(confLocal.flagMonitor){
          if(raw - valPedestal > valNoise * confLocal.seedSNR)
            plane.PushPixel(ix, iy, 1);
        }
        // analysis - corryvreckan::EventLoaderEUDAQ2
        else
          plane.PushPixel(ix, iy, raw - valPedestal); // Charge in ADC unit
      }else if(raw > thr){
        plane.PushPixel(ix, iy, 1);
      }
  }} // End - loop pixels
  
  out->AddPlane(plane);

  return true;
}

void CE65RawEvent2StdEventConverter::Dump(const std::vector<uint8_t> &data,size_t i) const {
  printf("[+] DEBUG : Dump event\n");
  for(int iy=0; iy < Y_MX_SIZE; iy++){
    printf("Y%2d\t",iy);
    for(int ix=0; ix < X_MX_SIZE; ix++){
      int iPixel = iy + ix * Y_MX_SIZE;
      uint8_t byteB = data[iPixel * sizeof(short) + 1];
      uint8_t byteA = data[iPixel * sizeof(short)];
      short valueFirst = short((byteB<<8)+byteA);
      printf("%d,", valueFirst);
    }
    printf("\b \n");
  }
}
