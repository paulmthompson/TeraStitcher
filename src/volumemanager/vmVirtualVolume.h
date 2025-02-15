//------------------------------------------------------------------------------------------------
// Copyright (c) 2012  Alessandro Bria and Giulio Iannello (University Campus Bio-Medico of Rome).  
// All rights reserved.
//------------------------------------------------------------------------------------------------

/*******************************************************************************************************************************************************************************************
*    LICENSE NOTICE
********************************************************************************************************************************************************************************************
*    By downloading/using/running/editing/changing any portion of codes in this package you agree to this license. If you do not agree to this license, do not download/use/run/edit/change
*    this code.
********************************************************************************************************************************************************************************************
*    1. This material is free for non-profit research, but needs a special license for any commercial purpose. Please contact Alessandro Bria at a.bria@unicas.it or Giulio Iannello at 
*       g.iannello@unicampus.it for further details.
*    2. You agree to appropriately cite this work in your related studies and publications.
*    3. This material is provided by  the copyright holders (Alessandro Bria  and  Giulio Iannello),  University Campus Bio-Medico and contributors "as is" and any express or implied war-
*       ranties, including, but  not limited to,  any implied warranties  of merchantability,  non-infringement, or fitness for a particular purpose are  disclaimed. In no event shall the
*       copyright owners, University Campus Bio-Medico, or contributors be liable for any direct, indirect, incidental, special, exemplary, or  consequential  damages  (including, but not 
*       limited to, procurement of substitute goods or services; loss of use, data, or profits;reasonable royalties; or business interruption) however caused  and on any theory of liabil-
*       ity, whether in contract, strict liability, or tort  (including negligence or otherwise) arising in any way out of the use of this software,  even if advised of the possibility of
*       such damage.
*    4. Neither the name of University  Campus Bio-Medico of Rome, nor Alessandro Bria and Giulio Iannello, may be used to endorse or  promote products  derived from this software without
*       specific prior written permission.
********************************************************************************************************************************************************************************************/

/******************
*    CHANGELOG    *
*******************
* 2019-09-20. Giulio.     @ADDED initialization of pointer to null pointer to prevent destructor from releasing a non-null pointer
* 2019-09-20. Giulio.     @ADDED data member 'mdata_filepath' with its getter
* 2018-03-02. Giulio.     @ADDED the possibility to set a path and a name for the mdata.bin file generated when volumes are created from data
* 2018-02-03. Giulio.     @ADDED internal method to correct non symmetric displacements between tiles
* 2018-01-20. Giulio.     @CHANGED some methods to virtual and the type of a few data members
* 2017-04-27. Giulio.     @ADDED static method to get the input plugin from the xml if specified
* 2017-04-12. Giulio.     @ADDED method to release all buffers allocated in VirtualStack
* 2016-09-01. Giulio.     @ADDED Cache buffer.
* 2016-06-09. Giulio      @ADDED the 'active_channel' field and corresponding getter and setter
* 2015-06-12. Giulio      @ADDED 'check' method to check completeness and coherence of a volume
* 2015-02-26. Giulio.     @ADDED fields DIM_C and BYTESxCHAN
* 2014-09-10. Alessandro. @ADDED 'getVolumeFormat' method to be applied on xml file.
* 2014-09-02. Alessandro. @FIXED possible bottleneck: N_SLICES was of 'uint16' type, now changed to 'int'. 'int' has been chosen in place of 'uint64'
*                         to avoid possible bugs as most of the code that uses 'N_SLICES' is 'int'-based. 
* 2014-09-01. Alessandro. @ADDED 'name2coordZ()' utility method to extract the z-coordinate substring from a given filename.
* 2014-08-30. Alessandro. @REMOVED 'extractPathFromFilePath()' method (obsolete).
* 2014-08-30. Alessandro. @REMOVED 'print()' method from abstract class and all concrete classes (obsolete).
*/

#ifndef _VM_VIRTUAL_VOLUME_H
#define _VM_VIRTUAL_VOLUME_H


#include <string>
#include <map>
#include "volumemanager.config.h"
#include "iomanager.config.h"
//#include "vmCacheManager.h"

class CacheBuffer;

//FORWARD-DECLARATIONS
class Displacement;

class volumemanager::VirtualVolume
{
    protected:

		//******OBJECT ATTRIBUTES******
		char* stacks_dir;					//C-string that contains the directory path of stacks matrix
		char* mdata_filepath;				//C-string that contains the complete path and name of the metadata binary file
		float  VXL_V, VXL_H, VXL_D;			//[microns]: voxel dimensions (in microns) along V(Vertical), H(horizontal) and D(Depth) axes
		float  ORG_V, ORG_H, ORG_D;			//[millimeters]: origin spatial coordinates (in millimeters) along VHD axes
		float  MEC_V, MEC_H;				//[microns]: mechanical displacements of the microscope between two adjacent stacks
		int N_ROWS, N_COLS;			//dimensions (in stacks) of stacks matrix along VH axes
		int N_SLICES;						//dimension along D(Z).
		vm::ref_sys reference_system;
		int    DIM_C;					// number of channels        (@ADDED by Iannello   on ..........)
		int    BYTESxCHAN;              // number of bytes per channel

		int active_channel;             // channel used to load image stacks 
		                                // it is initialized to channel 0 by default or to one of the first three channels using iom::CHANS or iom::CHANS_no
		                                // it can be set calling a setter 

		CacheBuffer *cb;

		//initialization methods
		virtual void init();
		virtual void applyReferenceSystem(vm::ref_sys reference_system, float VXL_1, float VXL_2, float VXL_3)=0;

		//binary metadata load/save methods
		virtual void saveBinaryMetadata(char *metadata_filepath)=0;
		virtual void loadBinaryMetadata(char *metadata_filepath)=0;

		//rotates stacks matrix around D vm::axis (accepted values are theta=0,90,180,270)
		virtual void rotate(int theta)=0;

		//mirrors stacks matrix along mrr_axis (accepted values are mrr_axis=1,2,3)
		virtual void mirror(vm::axis mrr_axis)=0;

		//extract spatial coordinates (in millimeters) of given Stack object reading directory and filenames as spatial coordinates
		void extractCoordinates(VirtualStack* stk, int z, int* crd_1, int* crd_2, int* crd_3=0);
		
		// 2018-02-03. Giulio. internal method to correct non symmetric displacements between tiles
		void adjustDisplacements ();

    public:

		//CONSTRUCTORS-DECONSTRUCTOR
		VirtualVolume(){
			init();

			cb = (CacheBuffer *) 0;
		}

		VirtualVolume(const char* _stacks_dir, vm::ref_sys _reference_system, float VXL_1=0, float VXL_2=0, float VXL_3=0)
		{
			//PMT
			//We are calling a virtual function from the constructor here, which isn't good practice
			//https://www.artima.com/articles/never-call-virtual-functions-during-construction-or-destruction
			init();

			stacks_dir = new char[strlen(_stacks_dir)+1];
			strcpy(stacks_dir, _stacks_dir);
			
			mdata_filepath = (char *) 0; // added to prevent destructor from releasing a non-null pointer

			reference_system = _reference_system;

			VXL_V = VXL_1;
			VXL_H = VXL_2;
			VXL_D = VXL_3;

			cb = (CacheBuffer *) 0;
		}

		VirtualVolume(const char* xml_path) {
			stacks_dir = (char *) 0;     // added to prevent destructor from releasing a non-null pointer
			mdata_filepath = (char *) 0; // added to prevent destructor from releasing a non-null pointer
			init();
			cb = (CacheBuffer *) 0;
		}

        virtual ~VirtualVolume();

		// ******GET METHODS******
		float	 getORG_V();
		float	 getORG_H();
		float	 getORG_D();
		float	 getABS_V(int ABS_PIXEL_V);
		float	 getABS_H(int ABS_PIXEL_H);
		float	 getABS_D(int ABS_PIXEL_D);
		float	 getVXL_V();
		float	 getVXL_H();
		float	 getVXL_D();
		float	 getMEC_V();
		float	 getMEC_H();
		virtual int getStacksHeight() = 0;
		virtual int	getStacksWidth() = 0;
		virtual int getN_ROWS();
		virtual int	getN_COLS();
		virtual int	getN_SLICES();
		int		 getDIM_C();
		int		 getBYTESxCHAN();
		virtual VirtualStack*** getSTACKS() = 0;
		char*    getSTACKS_DIR(){return this->stacks_dir;}
		char*    getMDATA_FILEPATH(){return this->mdata_filepath;}
		virtual int	getOVERLAP_V();
		virtual int getOVERLAP_H();
		virtual int	getDEFAULT_DISPLACEMENT_V();
		virtual int	getDEFAULT_DISPLACEMENT_H();
		virtual int	getDEFAULT_DISPLACEMENT_D();
		virtual vm::ref_sys getREF_SYS(){return reference_system;}

		int      getACTIVE_CHAN() { return active_channel; }
		void     setACTIVE_CHAN(int c) { active_channel = c; }

		CacheBuffer *getCACHEBUFFER() { return cb; }
		
		//loads/saves metadata from/in the given xml filename
		virtual void loadXML(const char *xml_filename) = 0;
		virtual void initFromXML(const char *xml_filename) = 0;
        virtual void saveXML(const char *xml_filename=0, const char *xml_filepath=0) = 0;

		virtual void releaseBuffers() = 0;

		// 2014-09-10. Alessandro. @ADDED 'getVolumeFormat' method to be applied on xml file.
		// return 'volume_format' attribute of <TeraStitcher> node from the given xml. 
        static std::string getVolumeFormat(const std::string& xml_path);

		// 2017-04-27. Giulio. @ADDED 'getInputPlugin' method to be applied on xml file.
		// return 'input_plugin' attribute of <TeraStitcher> node from the given xml if present
		// return an empty string if the attribute is not present (for compatibility reasons).
        static std::string getInputPlugin(const std::string& xml_path);

		//inserts the given displacement in the given stacks
		void insertDisplacement(VirtualStack *stk_A, VirtualStack *stk_B, Displacement *displacement);

        /**********************************************************************************
        * UTILITY methods
        ***********************************************************************************/
        //check if volume is complete and coherent; return true if the volume is ok, false otherwise
		//if a file name is passed and thevolume is not ok an error log file is generated
		virtual bool check(const char *errlogFileName = 0)=0;

		//counts the total number of displacements and the number of displacements per pair of adjacent stacks
        virtual void countDisplacements(int& total, float& per_stack_pair) = 0;

        //counts the number of single-direction displacements having a reliability measure above the given threshold
        virtual void countReliableSingleDirectionDisplacements(float threshold, int& total, int& reliable) = 0;

        //counts the number of stitchable stacks given the reliability threshold
        virtual int countStitchableStacks(float threshold) = 0;

		//returns true if file exists at the given filepath
		static bool fileExists(const char *filepath);

		// returns the Z-coordinate string extracted from the given filename. Supported filenames formats are
		// [0-9]+_[0-9]+_[0-9]+.*   and
		// [0-9]+.*
		static std::string name2coordZ(const std::string & filename);
};


// define new type: VirtualVolume plugin creator functions
typedef volumemanager::VirtualVolume* (*VolumeCreatorXML)(const char*, bool);
typedef volumemanager::VirtualVolume* (*VolumeCreatorData)(const char*, vm::ref_sys, float, float, float, bool, std::string);

// Factory for plugins' registration and instantiation
class volumemanager::VirtualVolumeFactory 
{
	private:

		// map of registered virtual volume plugins
		std::map<std::string, VolumeCreatorXML> creators_xml; 
		std::map<std::string, VolumeCreatorData> creators_data; 

	public:

		// singleton
		static VirtualVolumeFactory* instance()
		{
			static VirtualVolumeFactory* uniqueInstance = new VirtualVolumeFactory();
			return uniqueInstance;
		}

		// plugin registration
		static std::string registerPluginCreatorXML(VolumeCreatorXML creator, std::string id) 
		{
			instance()->creators_xml[id] = creator;
			return id;
		}		
		static std::string registerPluginCreatorData(VolumeCreatorData creator, std::string id) 
		{
			instance()->creators_data[id] = creator;
			return id;
		}	

		// plugin instantiation
        static VirtualVolume* createFromXML(const char* xml_path, bool ow_mdata)
		{ 
			std::string id = VirtualVolume::getVolumeFormat(xml_path);
			if(instance()->creators_xml.find(id) == instance()->creators_xml.end())
				throw iom::exception(iom::strprintf("Cannot find VirtualVolume(xml) plugin \"%s\": no such plugin", id.c_str()).c_str());
            return (instance()->creators_xml[id])(xml_path, ow_mdata);
		}
        static VirtualVolume* createFromXML(std::string id, const char* xml_path, bool ow_mdata)
        {
            if(instance()->creators_xml.find(id) == instance()->creators_xml.end())
                throw iom::exception(iom::strprintf("Cannot find VirtualVolume(xml) plugin \"%s\": no such plugin", id.c_str()).c_str());
            return (instance()->creators_xml[id])(xml_path, ow_mdata);
        }

		static VirtualVolume* createFromData(std::string id, const char* data_path, vm::ref_sys ref, float vxl1, float vxl2, float vxl3, bool ow_mdata, std::string mdata_fname="")
		{ 
			if(instance()->creators_data.find(id) == instance()->creators_data.end())
				throw iom::exception(iom::strprintf("Cannot find VirtualVolume(data) plugin \"%s\": no such plugin", id.c_str()).c_str());
			return (instance()->creators_data[id])(data_path, ref, vxl1, vxl2, vxl3, ow_mdata, mdata_fname); 
		}

		// get list of registered plugins
		static std::string registeredPlugins(){
			std::string plugins;
			for(std::map<std::string, VolumeCreatorXML>::iterator it = instance()->creators_xml.begin(); it != instance()->creators_xml.end(); it++)
				plugins += "\"" + it->first + "\", ";
			plugins = plugins.substr(0, plugins.find_last_of(","));
			return plugins;
        }
        static std::vector <std::string> registeredPluginsList(){
            std::vector <std::string> plugins;
            for(std::map<std::string, VolumeCreatorXML>::iterator it = instance()->creators_xml.begin(); it != instance()->creators_xml.end(); it++)
                plugins.push_back(it->first);
            return plugins;
        }
};

#endif
