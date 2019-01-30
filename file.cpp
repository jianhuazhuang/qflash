#include "file.h"
#include "download.h"
#include "tinyxml.h"
#include "os_linux.h"
#include "quectel_common.h"
#include "quectel_log.h"
#include "quectel_crc.h"

#include <string>
#include <vector>
#include "md5.h"
extern void qdl_pre_download(void);
extern void qdl_post_download(void);

typedef struct _md5_item{
	std::string filename;
	std::string md5_value;
}md5_item;

#ifdef PROGRESS_FILE_FAETURE

extern unsigned long g_cumulation_files_size;
extern unsigned long g_total_files_size;


unsigned long get_single_file_size(const char *fileName)    
{   
	unsigned long filesize = 0;        
	struct stat statbuff;    
	if(stat(fileName, &statbuff) < 0) {    
		return filesize;    
	}else{    
		filesize = statbuff.st_size;    
	}   
	
	return filesize; 
}

int writeFile(double updatePercent)
{
	FILE *fp;
	char buffer[8] = {0};
	const char* filePath = "/data/update.conf";
	if(NULL == (fp = fopen(filePath,"w+")))
	{
		dbg_time("open file fault !\n");
		return -1;
	}
	//dbg_time("func = %s, line = %d, %0.2f, percent = %.0lf\n", __FUNCTION__, __LINE__, updatePercent, (updatePercent * 100));

	sprintf(buffer, "%.0lf%%", (updatePercent * 100));

	if(0 == (fwrite (buffer , sizeof(char), strlen(buffer), fp)))
	{
		dbg_time("write file fault !\n");
		fclose (fp);
		return -1;
	}
	fflush(fp);
	fclose (fp);
	return 0;
}

unsigned long cumulation_files_size(const char *fileName)
{
	g_cumulation_files_size  += get_single_file_size(fileName);
	return g_cumulation_files_size;
}
double upgrade_percent()
{
	if(0 == g_total_files_size) 
		return 0.0;
	return (double)g_cumulation_files_size / g_total_files_size;
}

int constantly_schedule(const char *fileName)
{
	cumulation_files_size(fileName);
	return writeFile(upgrade_percent());
}
#endif

static int parse_line(const std::string& line, std::string &filename, std::string & md5)
{	
	int r1,r2;
	size_t n;
	if(line.find("START") != std::string::npos || line.find("VERSION") != std::string::npos || line.find("END") != std::string::npos)
		return -1;
	r1 = line.rfind(':');
	if(r1 == -1)	return 0;
	r2 = line.rfind('\\');
	if(r2 == -1) 	return 0;
	if(r2 < r1)
	{
		filename = line.substr(r2  + 1 , r1 - r2 - 1);	//get filename without path
		md5 = line.substr(r1 + 1);						//get md5 value		
	}else{
		return -1;
	}
	return 0;
}

static int parse_md5_file(const char* filename, std::vector<md5_item>& vec)
{
#define LINE_BYTES	1024
	int ret = -1;
	char line[LINE_BYTES];
	FILE *fp = NULL;
	std::string v1, v2;
	fp = fopen(filename, "r");
	if(fp == NULL)	goto done;
	while(fgets(line, LINE_BYTES - 1, fp))
	{
		if(!parse_line(line, v1, v2))
		{
			md5_item item;
			item.filename = v1;
			item.md5_value = v2;
			vec.push_back(item);
		}
	}
	if(vec.size() > 0) ret = 0;
	else
		ret = 1;	//not valid format
done:
	if(fp != NULL) fclose(fp);
	return ret;
}
static std::vector<md5_item> md5_vec;	//parse the md5.txt and save [filename:md5]

/*
note: vec md5 value include \n
*/
static int lookup_item(std::vector<md5_item>& md5_vec, char *filename, const char* md5)
{
	std::vector<md5_item>::iterator iter = md5_vec.begin();
	for(; iter != md5_vec.end(); ++iter)
	{
		if(	strstr(filename, (*iter).filename.c_str()) != NULL &&
			strstr((*iter).md5_value.c_str(), md5) != NULL)
		{
			return 0;
		}
		
	}
	return 1;		//cann't find
}
static int check_file_md5_value(char* filename, std::vector<md5_item>& md5_vec)
{
/*
1. calc the md5 about filename
2. lookup from md5_vec(build with md5.txt)
3. got it, return 0, or 1
*/
	unsigned char md5_hex[16] = {0};
	char md5_str[32 + 1];
	memset(md5_hex, 0, 16);
	if(-1 == md5sum((char*)filename, md5_hex))
	{
		dbg_time("calclate %s md5 failed.\n", filename);
		return -1;
	}

	sprintf(md5_str, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
    	md5_hex[0],md5_hex[1],md5_hex[2],md5_hex[3],
    	md5_hex[4],md5_hex[5],md5_hex[6],md5_hex[7],
    	md5_hex[8],md5_hex[9],md5_hex[10],md5_hex[11],
    	md5_hex[12],md5_hex[13],md5_hex[14],md5_hex[15]);
    if(lookup_item(md5_vec, filename,  md5_str) == 0)
    {
    	dbg_time("md5 checking: %s pass\n", filename);
    	return 0;
    }else
    {
    	dbg_time("md5 examine: %s fail\n", filename);
    }
    return 1;
}
static char* retrieve_md5_filename(const char* path)
{
	struct dirent *de;
	DIR *busdir;
	char *filename = NULL;
	
	busdir = opendir(path);
	if(busdir == 0) 
		return 0;
	while((de = readdir(busdir)))
	{			
		if(strcasecmp(de->d_name, "md5.txt") == 0)
		{			
			asprintf(&filename, "%s/%s", path, de->d_name);	//need free in caller
			break;
		}		
	}
	closedir(busdir);
	return filename;
}

unsigned char * open_file(const char *filepath, uint32 *filesize) {
    unsigned char *filebuf;
    struct stat sb;
    int fd;

    if (filesize == NULL)
        return NULL;

    fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        dbg_time("fail to open %s\n", filepath);
        return NULL;
    }

    if (fstat(fd, &sb) == -1) {
        dbg_time("fail to fstat %s\n", filepath);
        return NULL;
    }

#if 0 //some soc donnot support MMU, so donot support mmap
    filebuf = (byte *)mmap(0, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (filebuf == MAP_FAILED) {
	close(fd);
        dbg_time("fail to mmap %s\n", filepath);
        return NULL;
    }
 
    if (close(fd) == -1) {
        munmap(filebuf, sb.st_size);
        dbg_time("fail to close %s\n", filepath);
        return NULL;
    }
#else
    if (sb.st_size > (1024*1024)) {
        close(fd);
        dbg_time("%s %s 's size is %dKbytes, larger than 1MBytes\n!", __func__, filepath, (uint32)sb.st_size/(1024));
        return NULL;
    }

    filebuf = (unsigned char *)malloc(sb.st_size + 128);
    if (filebuf == NULL) {
        close(fd);
        dbg_time("fail to malloc for %s\n", filepath);
        return NULL;
    }	

    if(read(fd, filebuf, sb.st_size) != sb.st_size) {
        close(fd);
        dbg_time("fail to read for %s\n", filepath);
        return NULL;
    }

    close(fd);
#endif

    *filesize = sb.st_size;
    return filebuf;
}

void free_file(unsigned char *filebuf,uint32 filesize) {
    if (filebuf == NULL) return;
    
#if 0 //some soc donnot support MMU, so donot support mmap
    if (munmap(filebuf, filesize) == -1) {
        dbg_time("fail to munmap %p %u\n", filebuf, filesize);
    }
#else
    free(filebuf);
#endif
}


bool GetNodePointerByName(TiXmlElement* pRootEle,const char* strNodeName,TiXmlElement* &Node)  
{  
	if (strcmp(strNodeName,pRootEle->Value())==0)  
	{  
		Node = pRootEle;  
		return true;  
	}  
	TiXmlElement* pEle = pRootEle;    
	
	for (pEle = pRootEle->FirstChildElement(); pEle; pEle = pEle->NextSiblingElement())    
	{    
		if(GetNodePointerByName(pEle,strNodeName,Node))  
			return true;  
	}    
	return false;  
}   

int retrieve_nrpg_enrpg_filename(const char* path, char** nrpg_filename, char **enrpg_filename)
{
	DIR *pdir;
	struct dirent* ent = NULL;
	pdir = opendir(path);
	if(pdir)
	{
		while((ent = readdir(pdir)) != NULL)
		{
			if(!strncmp(ent->d_name, "NPRG", 4))
			{
				*nrpg_filename = strdup(ent->d_name);
			}
			if(!strncmp(ent->d_name, "ENPRG", 5))
			{
				*enrpg_filename = strdup(ent->d_name);
			}
			
		}
		closedir(pdir);
		return 0;
	}else
	{
		return 1;
	}
	return 1;
}

int retrieve_filename(download_context *ctx, char* path)
{
	struct dirent *de;
	DIR *busdir;
	char *filename = NULL;
	
	busdir = opendir(path);
	if(busdir == 0) 
		return 0;
	while((de = readdir(busdir)))
	{			
		if(strstr(de->d_name, "patch") != NULL)
		{
			asprintf(&ctx->patch_xml, "%s/%s", path, de->d_name);
		}else if(strstr(de->d_name, "partition") != NULL)
		{
			asprintf(&ctx->partition_complete_mbn, "%s/%s", path, de->d_name);			
		}else if(strstr(de->d_name, "raw") != NULL)
		{
			asprintf(&ctx->rawprogram_nand_update_xml, "%s/%s", path, de->d_name);
		}else if(strstr(de->d_name, "prog") != NULL)
		{
			asprintf(&ctx->prog_nand_firehose_mbn, "%s/%s", path, de->d_name);
		}else
		{
			//hello
		}		
	}
	closedir(busdir);
	if(	access(ctx->patch_xml, F_OK) != 0 || 
		access(ctx->partition_complete_mbn, F_OK) != 0 || 
		access(ctx->rawprogram_nand_update_xml, F_OK) != 0 || 
		access(ctx->prog_nand_firehose_mbn, F_OK) != 0)
	{
		dbg_time("firehose files can't access.\n");
		return -1;
	}
	dbg_time("firehose files check pass\n");
	return 0;
}

#ifdef PROGRESS_FILE_FAETURE
unsigned long get_total_files_size(download_context *ctx)
{
	vector<Ufile>::iterator iter; 
	for (iter=ctx->ufile_list.begin();iter!=ctx->ufile_list.end();/*iter++*/) 
	{
		if(strcmp("0:MIBIB",((Ufile)*iter).name)!=0)
		{
			g_total_files_size += get_single_file_size(((Ufile)*iter).img_name);
		}

		iter++;
	}
	dbg_time("file total size: %lu\n", g_total_files_size);
	return g_total_files_size;
}
#endif

int image_read(download_context *ctx) {
   
	//find contents.xml
	char *nrpg_filename = NULL;
	char *enrpg_filename = NULL;
	char *ptr = NULL;
	int ret = 0;
	TiXmlDocument *pDocNode = NULL;
	TiXmlDocument *pDoc = NULL;
	TiXmlElement *pRootEle = NULL;
	TiXmlElement *pNode = NULL;
	char* partition_nand_path = NULL;
	long long all_files_bytes = 0;
	char temp[256 + 1] ={0};
	vector<Ufile>::iterator iter; 
	int md5ret;
	char* md5_file_path = NULL;
	
	asprintf(&ctx->contents_xml_path,"%s/%s", ctx->firmware_path, "contents.xml");
	if(access(ctx->contents_xml_path, F_OK))
	{
		dbg_time("Not found contents.xml\n");
		return 0;
	}
	//check md5 file whether exist
	if( (md5_file_path = retrieve_md5_filename(ctx->firmware_path)) != 0)
	{
		dbg_time("Detect %s file.\n", md5_file_path);
		//get md5 and filename pair
		if(parse_md5_file(md5_file_path, md5_vec) != 0)
		{
			dbg_time("Warnning: md5 file format error, ignore md5 check\n");
			//return 0;
		}else{
			dbg_time("md5 checking enable.\n");
			ctx->md5_check_enable = 1;
		}
	}else{
		//print nothing.
	}
	
	if( ctx->md5_check_enable)
	{
		if(0 != check_file_md5_value(ctx->contents_xml_path, md5_vec))
		{
			ret = 0;
			goto __exit_image_read;
		}
	}
	pDoc  = new TiXmlDocument();
	if (NULL==pDoc)  
	{  	
		return 0;  
	}  
	pDoc->LoadFile(ctx->contents_xml_path); 
	pRootEle = pDoc->RootElement();  
	if (NULL==pRootEle)  
	{  
		return 0;  
	}  
	
	if(GetNodePointerByName(pRootEle,"partition_file",pNode)==false)
		return 0;
	if (NULL!=pNode)  
	{  
		TiXmlElement *NameElement = pNode->FirstChildElement();
		asprintf(&partition_nand_path,"%s/%s%s",ctx->firmware_path,NameElement->NextSiblingElement()->GetText(),NameElement->GetText());
		ptr = ctx->firmware_path;
		asprintf(&ctx->firmware_path,"%s/%s",ctx->firmware_path,NameElement->NextSiblingElement()->GetText());
		if(ptr)
		{
			free(ptr);			
		}
		
	} 
	//dbg_time("%s\n",partition_nand_path);
	if(access(partition_nand_path, F_OK))
	{
		dbg_time("Not found partition_nand.xml\n");
		ret = 0;
		goto __exit_image_read;
	}
	delete pDoc;

	if(ctx->md5_check_enable )
	{
		if(0 != check_file_md5_value(partition_nand_path, md5_vec))
		{
			ret = 0; 
			goto __exit_image_read;
		}
	}

	pDocNode  = new TiXmlDocument();
	if (NULL==pDocNode)  
	{  
		ret = 0;
		goto __exit_image_read;  
	}  
	pDocNode->LoadFile(partition_nand_path);
	pRootEle= pDocNode->RootElement();
	if (NULL==pRootEle)  
	{  
		ret = 0;
		goto __exit_image_read; 
	}  	
	pNode = NULL;  
	if(GetNodePointerByName(pRootEle,"partitions",pNode)==false)
		return 0;
	if (NULL!=pNode)  
	{
		for (TiXmlElement * pEle = pNode->FirstChildElement(); pEle; pEle = pEle->NextSiblingElement())    
		{
			
			Ufile ufile = {0};
			int i = 0;
			for (TiXmlElement * tmp=pEle->FirstChildElement();tmp;tmp=tmp->NextSiblingElement())
			{
				if(strcmp("name",tmp->Value())==0)
				{
					asprintf(&ufile.name,"%s",tmp->GetText());
					i++;
					{
						char * p = strstr(ufile.name, ":");
						if(p == NULL)
						{
							dbg_time("error, parse partition name failed!");
						}else
						{
							p++; //skip :
							asprintf(&ufile.partition_name, "%s", p);
						}
						
					}
				}
				if(strcmp("img_name",tmp->Value())==0)
				{
					asprintf(&ufile.img_name,"%s/%s",ctx->firmware_path,tmp->GetText());
					i++;

					if( ctx->md5_check_enable )
					{
						if(0 != check_file_md5_value(ufile.img_name, md5_vec))
						{
							ret = 0; 
							goto __exit_image_read;
						}
					}

				}
			}
			if(i==2)
			{
				ctx->ufile_list.push_back(ufile);
			}
			else
			{			
				if(ufile.img_name)
				{
					free(ufile.img_name);
					ufile.img_name = NULL;
				}
				if(ufile.name)
				{
					free(ufile.name);
					ufile.name = NULL;
				}
				if(ufile.partition_name)
				{
					free(ufile.partition_name);
					ufile.partition_name = NULL;
				}
			}
		}
	}
	 
    for (iter=ctx->ufile_list.begin();iter!=ctx->ufile_list.end();iter++)  
    {  
		if(strcmp("0:MIBIB",((Ufile)*iter).name)==0)
		{
			asprintf(&ctx->partition_path,"%s",((Ufile)*iter).img_name);
		}		
		all_files_bytes += get_file_size(((Ufile)*iter).img_name);
    }
    transfer_statistics::getInstance()->set_total(all_files_bytes);
	if(retrieve_nrpg_enrpg_filename(ctx->firmware_path, &nrpg_filename, &enrpg_filename) != 0)
	{
		ret = 0;
		goto __exit_image_read;
	}
	if(nrpg_filename == NULL || enrpg_filename == NULL)
	{
		ret = 0;
		goto __exit_image_read;
	}	
	asprintf(&ctx->NPRG_path,"%s/%s",ctx->firmware_path,nrpg_filename);
	asprintf(&ctx->ENPRG_path,"%s/%s",ctx->firmware_path,enrpg_filename);

	ctx->platform = get_module_platform(ctx->NPRG_path);
	if(ctx->platform == platform_unknown)
	{
		dbg_time("error:	cann't detect firmware platfrom!\n");
		ret = -1;
		goto __exit_image_read;
	}
	

	if(ctx->md5_check_enable)
	{
		if(	(0 != check_file_md5_value(ctx->NPRG_path, md5_vec)) || 
			(0 != check_file_md5_value(ctx->ENPRG_path, md5_vec))) 
		{
			ret = 0; 
			goto __exit_image_read;
		}		
	}
	{
		asprintf(&ctx->firehose_path, "%s/%s", ctx->firmware_path, "firehose");
		if(access(ctx->firehose_path, F_OK) != 0)
		{
			dbg_time("firehose direcotry missing, firehose upgarde not supported\n");
		}else
		{
			dbg_time("find firehose directory!\n");
			ctx->firehose_support = 1;
		}
		if(ctx->firehose_support == 1 && retrieve_filename(ctx, ctx->firehose_path) != 0)
		{
			ret = 0;
			goto __exit_image_read;
		}
	}
#ifdef PROGRESS_FILE_FAETURE	
	get_total_files_size(ctx);
#endif	
	ret = 1;
__exit_image_read:	
	if(pDocNode != NULL)			delete pDocNode;
	if(partition_nand_path != NULL) delete partition_nand_path;
	if(nrpg_filename) delete nrpg_filename;
	if(enrpg_filename) delete enrpg_filename;
	if(md5_file_path) delete md5_file_path;
    return ret;
}

int image_close(download_context *ctx)
{
	delete ctx->firmware_path;
	ctx->firmware_path = NULL;
	delete ctx->contents_xml_path;
	ctx->contents_xml_path = NULL;
	delete ctx->NPRG_path;
	ctx->NPRG_path = NULL;
	delete ctx->ENPRG_path;
	ctx->ENPRG_path = NULL;
	delete ctx->partition_path;
	ctx->partition_path = NULL;
	vector<Ufile>::iterator iter;  
    for (iter=ctx->ufile_list.begin();iter!=ctx->ufile_list.end();iter++)  
    {  
		if( (*iter).name != NULL)
		{
			free((*iter).name);
		}
		if((*iter).img_name != NULL)
		{
			free((*iter).img_name);
		}
		if((*iter).partition_name != NULL)
		{
			free((*iter).partition_name);
		}
    }
    
	if(ctx->diag_port)
	{
		free(ctx->diag_port);
	}
	return 1;
}


