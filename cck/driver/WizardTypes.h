#ifndef WIZARDTYPES
#define WIZARDTYPES

#define MIN_SIZE 256
#define MID_SIZE 512
#define MAX_SIZE 1024
#define EXTD_MAX_SIZE 10240

// Some global structures
typedef struct NVPAIR
{
	char name[MID_SIZE];
	char value[MID_SIZE];
	char options[MAX_SIZE];
	char type[MID_SIZE];
}NVPAIR;

typedef struct ACTIONSET
{
	CString event;
	CString dll;
	CString function;
	char parameters[MAX_SIZE];
}ACTIONSET;

typedef struct DIMENSION
{
	int width;
	int height;
}DIMENSION;

typedef struct OPTIONS
{
	char* name[25];
	char* value[25];
}OPTIONS;

typedef struct WIDGET
{
	CString type;
	CString name;
	CString value;
	CString title;
	CString group;
	CString target;
	CString description;
	POINT location;
	DIMENSION size;
	ACTIONSET action;
	int numOfOptions;
	OPTIONS options;
	CString items;
	BOOL cached;
	int widgetID;
	CWnd *control;
}WIDGET;


typedef struct IMAGE
{
	CString name;
	CString value;
	POINT location;
	DIMENSION size;	
	HBITMAP hBitmap;
}IMAGE;

typedef struct VARS
{
	CString title;
	CString caption;
	CString pageName;
	CString image;
	CString visibility;
	CString functionality;
}VARS;

typedef struct PAGE
{
	CStringArray pages;
	CStringArray visibility;
}PAGE;

typedef struct CONTROLS
{
	CString onNextAction;
	CString helpFile;
}CONTROLS;

typedef struct WIDGETGROUPS
{
	CString groupName;
	CString widgets;
}WIDGETGROUPS;

typedef struct NODE
{
	NODE *parent;
	NODE **childNodes;
	int numChildNodes;
	int currNodeIndex;
	VARS *localVars;
	PAGE *subPages;
	CONTROLS *navControls;
	WIDGET** pageWidgets;
	int numWidgets;
	int currWidgetIndex;
	int pageBaseIndex;
	IMAGE **images;
	int numImages;
	BOOL nodeBuilt;
	BOOL isWidgetsSorted;
}NODE;

#endif //WIZARDTYPES
