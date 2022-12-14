/* $Id: Gridbox.c,v 2.4 2002/05/08 20:08:22 falk Exp falk $
 *
 * Gridbox.c - Gridbox composite widget
 *
 * Author: Edward A. Falk
 *	   falk@efalk.org
 *
 * Date: August 1998
 *
 *
 * The Gridbox widget aligns its children in a rectangular array
 * of cells.  Child widgets occupy a rectangular region of cells (default
 * size is 1x1).  A typical layout might look like this:
 *
 *	+-------+-------+--+----------------------------+
 *	|	|	|  |				|
 *	|-------+-------|  |				|
 *	|	|	|  |				|
 *	|-------+-------|  |				|
 *	|	|	|  |				|
 *	|-------+-------|  |				|
 *	|	|	|  |				|
 *	|-------+-------+--+----------------------------|
 *	|__________________|____________________________|
 *
 * In addition, child widgets may specify a margin, and weights which
 * determine how they are resized if the parent widget is resized.
 *
 * $Log: Gridbox.c,v $
 * Revision 2.4  2002/05/08 20:08:22  falk
 * added allowResize child constraint.  Improved layout management.
 *
 * Revision 2.3  2000/01/29 22:02:03  falk
 * Now recomputes child sizes always.
 *
 * Revision 2.2  1999/12/12 07:53:37  falk
 * minor cleanup in geometry management.
 *
 * Revision 2.1	 1999/12/12 06:05:06  falk
 * Added "next" and "same" grid position values.
 * Fixed bugs related to malloc/free.
 *
 * Revision 2.0	 1999/10/19 21:48:34  falk
 * Motif compatibility
 *
 * Revision 1.7	 1999/07/30 17:11:57  falk
 * now ignores unmanaged children
 *
 * Revision 1.6	 1999/07/17 19:54:14  falk
 * fixed bug where child doesn't accept a compromise but gridbox has already
 * resized itself.
 *
 * Revision 1.5	 1999/07/17 15:31:07  falk
 * Re-arranged layout code.  Uses fill constraint even on initial layout
 * and child resize request.  Fixed bug when child asks to shrink.
 *
 * Revision 1.4	 1999/07/01 16:33:56  falk
 * Added more caching of width/height info.  Handles query-only and
 * rejected requests much better.
 *
 * Revision 1.3	 1999/03/26 18:50:54  falk
 * Does not attempt to recompute anything if no children
 *
 * Revision 1.2	 1998/08/07 03:07:24  falk
 * string => fill style converter slightly more robust
 *
 * Revision 1.1	 1998/08/06 23:27:06  falk
 * Initial revision
 *
 *
 * General theory of operation:
 *
 * Each child widget has its own "preferred" size, which is queried during
 * the geometry management process.
 *
 * Gridbox maintains arrays of preferred column widths and row heights
 * based on the maximum values of the child widgets in those rows and
 * columns.  Gridbox computes its own preferred size from this information.
 *
 * Gridbox always returns its own preferred size in response to query_geometry()
 * requests.
 *
 * When a child widget asks to be resized, Gridbox updates the cached preferred
 * size for the child, recomputes its own preferred size accordingly, and asks
 * its parent to be resized.  Once negotiations with the parent are complete,
 * Gridbox then computes the new size of the child and responds to the child's
 * request.
 *
 * Whenever the Gridbox is resized, it determines how much extra space there
 * is (if any), and distributes it among the rows and columns based on the
 * weights of those rows & columns.
 *
 * Note:  Specifications (and loop prevention) require that if Gridbox
 * cannot accomodate a child request and offers a compromise, that
 * compromise must be accepted if the child asks for it.
 *
 *
 * Internal functions related to geometry management:
 *
 * getPreferredSizes()	obtains preferred sizes from child widgets.
 * computeWidHgtInfo()	based on preferred sizes, find row/column sizes
 * computeWidHgtMax()	based on preferred sizes, find max sizes
 * GridboxResize()	given Gridbox size, lay out the child widgets.
 * layout()		given size, assign sizes of rows & columns
 * layoutChild()	assign size of one child widget
 * changeGeometry()	attempt to change size, negotiate with parent
 *
 */

#include <stdio.h>

#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xmu/Converters.h>
#include <X11/Xmu/CharSet.h>
#include "GridboxP.h"

#define DEFAULT_MARGIN	-1

#define Offset(field) XtOffsetOf(GridboxRec, gridbox.field)
static XtResource resources[] = {
    {XtNdefaultDistance, XtCThickness, XtRInt, sizeof(int),
	Offset(defaultDistance), XtRImmediate, (XtPointer)4},
};
#undef Offset

#define Offset(field) XtOffsetOf(GridboxConstraintsRec, gridbox.field)
static XtResource gridboxConstraintResources[] = {
    {XtNgridx, XtCPosition, XtRGridPosition, sizeof(Position),
	Offset(gridx), XtRImmediate, (XtPointer)0},
    {XtNgridy, XtCPosition, XtRGridPosition, sizeof(Position),
	Offset(gridy), XtRImmediate, (XtPointer)0},
    {XtNgridWidth, XtCWidth, XtRDimension, sizeof(Dimension),
	Offset(gridWidth), XtRImmediate, (XtPointer)1},
    {XtNgridHeight, XtCHeight, XtRDimension, sizeof(Dimension),
	Offset(gridHeight), XtRImmediate, (XtPointer)1},
    {XtNfill, XtCFill, XtRFillType, sizeof(FillType),
	Offset(fill), XtRImmediate, (XtPointer)FillBoth},
    {XtNgravity, XtCGravity, XtRGravity, sizeof(XtGravity),
	Offset(gravity), XtRImmediate, (XtPointer)CenterGravity},
    {XtNweightx, XtCWeight, XtRInt, sizeof(int),
	Offset(weightx), XtRImmediate, (XtPointer)0},
    {XtNweighty, XtCWeight, XtRInt, sizeof(int),
	Offset(weighty), XtRImmediate, (XtPointer)0},
    {XtNmargin, XtCMargin, XtRInt, sizeof(int),
	Offset(margin), XtRImmediate, (XtPointer)DEFAULT_MARGIN},
    {XtNallowResize, XtCAllowResize, XtRBoolean, sizeof(Boolean),
	Offset(allowResize), XtRImmediate, (XtPointer)True},
};
#undef Offset

#ifdef	__STDC__
static	void	GridboxClassInit() ;
static	void	GridboxInit(Widget request, Widget new, ArgList, Cardinal *) ;
static	void	GridboxExpose(Widget w, XEvent *event, Region region) ;
static	void	GridboxResize(Widget w) ;
static	Boolean GridboxSetValues(Widget, Widget, Widget, ArgList, Cardinal *) ;
static	void	GridboxDestroy(Widget w) ;
static	XtGeometryResult
	  GridboxQueryGeometry(Widget, XtWidgetGeometry *, XtWidgetGeometry *) ;

static	void	GridboxChangeManaged(Widget w) ;
static	XtGeometryResult
	GridboxGeometryManager(Widget, XtWidgetGeometry *, XtWidgetGeometry *) ;

static	void	GridboxConstraintInit(Widget, Widget, ArgList, Cardinal *) ;
static	Boolean
	GridboxConstraintSetValues(Widget, Widget, Widget, ArgList, Cardinal *);


static	void	getPreferredSizes(GridboxWidget) ;
static	void	computeCellSize(GridboxWidget, GridboxConstraints,
			Dimension *,Dimension *);
static	void	allocAll(GridboxWidget) ;
static	void	freeAll(GridboxWidget) ;
static	void	computeWidHgtInfo(GridboxWidget) ;
static	void	computeWidHgtMax(GridboxWidget) ;
static	void	computeWidHgtUtil(int, int, int, int, Dimension *, int *) ;
static	void	layout(GridboxWidget, int, int) ;
static	void	layoutChild(GridboxWidget, Widget, Dimension *, Dimension *,
			Position *, Position *) ;
static	XtGeometryResult
	      changeGeometry(GridboxWidget, int, int, int, XtWidgetGeometry *) ;

static	Boolean _CvtStringToFillType(Display *, XrmValuePtr, Cardinal *,
			XrmValuePtr, XrmValuePtr, XtPointer *) ;
static	Boolean _CvtStringToGridPosition(Display *, XrmValuePtr, Cardinal *,
			XrmValuePtr, XrmValuePtr, XtPointer *) ;
#else
static	void	GridboxClassInit() ;
static	void	GridboxInit() ;
static	void	GridboxExpose() ;
static	void	GridboxResize() ;
static	Boolean GridboxSetValues() ;
static	void	GridboxDestroy() ;
static	void	GridboxChangeManaged() ;
static	void	GridboxConstraintInit() ;
static	Boolean GridboxConstraintSetValues() ;
static	void	getPreferredSizes() ;
static	void	computeCellSize() ;
static	void	freeAll() ;
static	void	computeWidHgtInfo() ;
static	void	computeWidHgtUtil() ;
static	void	layout() ;
static	void	layoutChild() ;
static	XtGeometryResult	GridboxQueryGeometry() ;
static	XtGeometryResult	GridboxGeometryManager() ;
static	XtGeometryResult	changeGeometry() ;
static	Boolean _CvtStringToFillType() ;
static	Boolean _CvtStringToGridPosition() ;
#endif

#define XTCALLOC(n,type)	((type *) XtCalloc((n), sizeof(type)))

#ifndef min
#define min(a,b)	((a)<(b)?(a):(b))
#define max(a,b)	((a)>(b)?(a):(b))
#endif


#ifdef	DEBUG
#define assert(e)	do { if( !(e) ) assfail(#e,__LINE__);} while(0)
static	void
assfail(char *e, int line)
{
  fprintf(stderr, "yak! assertion failed: %s, %s line %d\n",
    e, __FILE__, line) ;
}
#else
#define assert(e)
#endif




#ifndef USE_MOTIF
#define SuperClass	(&constraintClassRec)
#else
#define SuperClass	(&xmManagerClassRec)
#endif

GridboxClassRec gridboxClassRec = {
  { /* core_class fields */
    /* superclass	  */	(WidgetClass) SuperClass,
    /* class_name	  */	"Gridbox",
    /* widget_size	  */	sizeof(GridboxRec),
    /* class_initialize	  */	GridboxClassInit,
    /* class_part_init	  */	NULL,
    /* class_inited	  */	FALSE,
    /* initialize	  */	GridboxInit,
    /* initialize_hook	  */	NULL,
    /* realize		  */	XtInheritRealize,
    /* actions		  */	NULL,
    /* num_actions	  */	0,
    /* resources	  */	resources,
    /* num_resources	  */	XtNumber(resources),
    /* xrm_class	  */	NULLQUARK,
    /* compress_motion	  */	TRUE,
    /* compress_exposure  */	TRUE,
    /* compress_enterleave*/	TRUE,
    /* visible_interest	  */	FALSE,
    /* destroy		  */	GridboxDestroy,
    /* resize		  */	GridboxResize,
    /* expose		  */	GridboxExpose,
    /* set_values	  */	GridboxSetValues,
    /* set_values_hook	  */	NULL,
    /* set_values_almost  */	XtInheritSetValuesAlmost,
    /* get_values_hook	  */	NULL,
    /* accept_focus	  */	NULL,
    /* version		  */	XtVersion,
    /* callback_private	  */	NULL,
    /* tm_table		  */	NULL,
    /* query_geometry	  */	GridboxQueryGeometry,
    /* display_accelerator*/	XtInheritDisplayAccelerator,
    /* extension	  */	NULL
  },
  { /* composite_class fields */
    /* geometry_manager	  */   GridboxGeometryManager,
    /* change_managed	  */   GridboxChangeManaged,
    /* insert_child	  */   XtInheritInsertChild,
    /* delete_child	  */   XtInheritDeleteChild,
    /* extension	  */   NULL
  },
  { /* constraint_class fields */
    /* subresourses	  */   gridboxConstraintResources,
    /* subresource_count  */   XtNumber(gridboxConstraintResources),
    /* constraint_size	  */   sizeof(GridboxConstraintsRec),
    /* initialize	  */   GridboxConstraintInit,
    /* destroy		  */   NULL,
    /* set_values	  */   GridboxConstraintSetValues,
    /* extension	  */   NULL
  },
#ifdef	USE_MOTIF
/* Manager Class fields */
  {
    /* translations		*/	NULL,
    /* syn_resources		*/	NULL,
    /* num_syn_resources	*/	0,
    /* syn_constraint_resources */	NULL,
    /* num_syn_constraint_resources */	0,
    /* parent_process		*/	XmInheritParentProcess,
    /* extension		*/	NULL
  },
#endif
  { /* gridbox_class fields */
    /* extension	*/   NULL
  }
};

WidgetClass gridboxWidgetClass = (WidgetClass)&gridboxClassRec;

/****************************************************************
 *
 * Class Procedures
 *
 ****************************************************************/


static	void
GridboxClassInit()
{
    XtAddConverter( XtRString, XtRGravity, XmuCvtStringToGravity, NULL, 0) ;
    XtSetTypeConverter( XtRString, XtRFillType, _CvtStringToFillType,
	NULL, 0, XtCacheNone, (XtDestructor)NULL);
    XtSetTypeConverter( XtRString, XtRGridPosition, _CvtStringToGridPosition,
	NULL, 0, XtCacheNone, (XtDestructor)NULL);
}


/* ARGSUSED */
static	void
GridboxInit(request, new, args, num_args)
    Widget request, new;
    ArgList args;
    Cardinal *num_args;
{
    GridboxWidget gb = (GridboxWidget)new;

    gb->gridbox.nx = gb->gridbox.ny = 0 ;
    gb->gridbox.max_wids = gb->gridbox.max_hgts = NULL ;
    gb->gridbox.wids = gb->gridbox.hgts = NULL ;
    gb->gridbox.total_wid = gb->gridbox.total_hgt = 0 ;
    gb->gridbox.max_weightx = gb->gridbox.max_weighty = NULL ;
    gb->gridbox.needs_layout = True ;

    /* TODO: I think that there are no children at this point, so there's
     * really no point in doing any geometry management now.  All of
     * this code may be unnecessary.
     */

    getPreferredSizes(gb) ;

    if( request->core.width == 0 || request->core.height == 0 )
    {
      if( gb->gridbox.max_wids == NULL )
	computeWidHgtInfo(gb) ;

      if( request->core.width == 0 ) new->core.width = gb->gridbox.total_wid;
      if( request->core.height == 0 ) new->core.height = gb->gridbox.total_hgt;
    }

    /* TODO: resize now, or wait until later? */
}



/* No realize function */


/* ARGSUSED */
static	void
GridboxExpose(w, event, region)
	Widget	w ;
	XEvent	*event ;
	Region	region ;
{
	GridboxWidget gb = (GridboxWidget)w ;

	if( gb->gridbox.needs_layout )
	  XtClass(w)->core_class.resize(w) ;
}



static	void
GridboxResize(w)
    Widget w;
{
    GridboxWidget gb = (GridboxWidget)w;
    WidgetList children = gb->composite.children;
    int num_children = gb->composite.num_children;
    Widget	*childP;
    Position	x, y;
    Dimension	width, height;
    int		i ;
    int		margin ;
    Dimension	*wids, *hgts ;
    Position	*xs, *ys ;

    /* determine how much space the rows & columns need */

    if( gb->gridbox.max_wids == NULL )
      computeWidHgtInfo(gb) ;

    if( gb->gridbox.nx <= 0 || gb->gridbox.ny <= 0 )
      return ;

    /* assign row & column sizes */
    layout(gb, gb->core.width, gb->core.height) ;

    /* assign positions */
    wids =  gb->gridbox.wids ;
    xs =  XTCALLOC(gb->gridbox.nx, Position) ;
    for(x=0, i=0; i < gb->gridbox.nx; ++i)
    {
      xs[i] = x ;
      x += wids[i] ;
    }

    /* Same again, for heights */
    hgts = gb->gridbox.hgts ;
    ys = XTCALLOC(gb->gridbox.ny, Position) ;
    for(y=0, i=0; i < gb->gridbox.ny; ++i)
    {
      ys[i] = y ;
      y += hgts[i] ;
    }

    /* Finally, loop through children, assign positions and sizes */
    /* Each child is assigned a size which is a function of its position
     * and size in cells.  The child's margin is subtracted from all sides.
     */

    for (childP = children; childP - children < num_children; childP++)
      if( XtIsManaged(*childP) )
      {
	GridboxConstraints gc = (GridboxConstraints)(*childP)->core.constraints;
	if (!XtIsManaged(*childP)) continue;

	margin = gc->gridbox.margin ;
	x = xs[gc->gridbox.gridx] + margin ;
	y = ys[gc->gridbox.gridy] + margin ;

	layoutChild(gb, *childP, &width, &height, &x, &y) ;

	XtConfigureWidget(*childP,x,y, width, height,
	  (*childP)->core.border_width );
      }
    gb->gridbox.needs_layout = False ;
    XtFree((char *)xs) ;
    XtFree((char *)ys) ;
}


/* ARGSUSED */
static	Boolean
GridboxSetValues(current, request, new, args, num_args)
    Widget current, request, new;
    ArgList args;
    Cardinal *num_args;
{
    /* The only resource is the default margin.	 I don't
     * think there's any reason to react to changes therein, so
     * this function does nothing.
     */

    return( FALSE );
}



/* No accept_focus function */


static	void
GridboxDestroy(w)
	Widget	w ;
{
	freeAll( (GridboxWidget)w ) ;
}



	/* Called when parent wants to know our preferred size */

static	XtGeometryResult
GridboxQueryGeometry( widget, request, reply  )
    Widget widget;
    XtWidgetGeometry *request, *reply;
{
    GridboxWidget gb = (GridboxWidget)widget;

    /* determine how much space the rows & columns need */

    if( gb->gridbox.max_wids == NULL ) {
      getPreferredSizes(gb) ;
      computeWidHgtInfo(gb) ;
    }

    reply->request_mode = CWWidth | CWHeight;
    reply->width = gb->gridbox.total_wid;
    reply->height = gb->gridbox.total_hgt;

    /* We always offer our preferred size as a compromise.  */

    if( (request->request_mode & CWWidth) &&
		request->width == gb->core.width  &&
	(request->request_mode & CWHeight) &&
		request->height == gb->core.height )
      return XtGeometryNo;

    return XtGeometryAlmost;
}





	/* COMPOSITE WIDGET FUNCTIONS */


static	void
GridboxChangeManaged(w)
    Widget w;
{
    GridboxWidget gb = (GridboxWidget)w;
    XtWidgetGeometry reply;
    int		width, height ;

    getPreferredSizes(gb) ;
    computeWidHgtInfo(gb) ;
    width = gb->gridbox.total_wid ;
    height = gb->gridbox.total_hgt ;

    /* ask to change geometry to accomodate; accept any compromise offered */
    if( changeGeometry(gb, width, height, False, &reply) == XtGeometryAlmost )
      (void) changeGeometry(gb, reply.width, reply.height, False, &reply) ;

    /* always re-execute layout */
    XtClass(w)->core_class.resize((Widget)gb) ;
}




	/* Respond to size change requests from a child.
	 *
	 * Recompute row/column sizes based on child request
	 * and request to change my own size accordingly.
	 *
	 * If allowResize is False, only grant child resize requests
	 * if they don't exceed current cell size.
	 *
	 * If parent grants; good.
	 * If parent offers compromise, accept.
	 * If parent refuses, live with it.
	 * Now that we have our own size, try to grant child
	 * request within those constraints.
	 *
	 * RULE:  If we offer the child a compromise; it must be
	 * a compromise we'll accept on the next call.  Lesstif will
	 * squawk if we don't do this.  Also, infinite loops can
	 * result.
	 */

static	XtGeometryResult
GridboxGeometryManager(w, request, reply)
    Widget w;
    XtWidgetGeometry *request;
    XtWidgetGeometry *reply;	/* RETURN */
{
    int			new_width, new_height ;
    int			old_width, old_height ;
    int			old_cw, old_ch ;
    Dimension		cell_width, cell_height ;
    GridboxWidget	gb = (GridboxWidget) XtParent(w);
    GridboxConstraints	gc = (GridboxConstraints) w->core.constraints;
    XtWidgetGeometry	myreply ;
    XtGeometryResult	result ;
    int			queryOnly = request->request_mode & XtCWQueryOnly ;
    int			margin ;
    Position		x,y ;

    /* Position requests always denied */

    if( ((request->request_mode & CWX) && request->x != w->core.x)  ||
	((request->request_mode & CWY) && request->y != w->core.y) )
      return XtGeometryNo ;

    /* Make all three fields in the request valid */
    if( !(request->request_mode & CWWidth) )
	request->width = w->core.width;
    if( !(request->request_mode & CWHeight) )
	request->height = w->core.height;
    if( !(request->request_mode & CWBorderWidth) )
	request->border_width = w->core.border_width;

#ifdef	COMMENT
    /* First, remember how much space we wanted before request */
    old_width = gb->gridbox.total_wid ;
    old_height = gb->gridbox.total_hgt ;
#endif	/* COMMENT */
    /* First, remember how much space we had before request */
    /* note to self: I changed this because of the case where I wanted
     * size 'X' but had size 'Y' and then asked for (and was granted)
     * size 'X'.  This was a size change that I wasn't detecting.
     * TODO: find out why I did it the old way; delete these comments.
     */
    old_width = gb->core.width ;
    old_height = gb->core.height ;

    /* And how much the child wanted */
    old_cw = gc->gridbox.prefWidth ;
    old_ch = gc->gridbox.prefHeight ;


    /* set child's preferred size to the request value */
    margin = 2*request->border_width + 2*gc->gridbox.margin ;
    gc->gridbox.prefWidth = request->width + margin ;
    gc->gridbox.prefHeight = request->height + margin ;

    if( gc->gridbox.allowResize )
    {
      /* recompute minimum row & column sizes */
      /* TODO: can this be short-cutted to only compute the
       * affected rows & columns?
       */
      if( gb->gridbox.needs_layout )
	computeWidHgtInfo(gb) ;
      else
	computeWidHgtMax(gb) ;
      new_width = gb->gridbox.total_wid ;
      new_height = gb->gridbox.total_hgt ;



      /* resize myself to accomodate request; make this a query to start;
       * the child may not want the compromise I offer.
       */

      result = changeGeometry(gb, new_width, new_height, True, &myreply) ;


      /* Recompute all column & row sizes. */

      layout(gb, myreply.width, myreply.height) ;


      /* Now, compute the new size of the child within the constraints */

      layoutChild(gb, w, &cell_width, &cell_height, &x,&y) ;



      if( queryOnly )
      {
	/* put things back the way they were */
	gc->gridbox.prefWidth = old_cw ;
	gc->gridbox.prefHeight = old_ch ;
	computeWidHgtMax(gb) ;
	if( result != XtGeometryNo )
	  layout(gb, old_width, old_height) ;
      }


      /* can't change */
      if( cell_width == w->core.width && cell_height == w->core.height )
	return XtGeometryNo ;

      /* request granted */
      if( cell_width == request->width && cell_height == request->height )
      {
	if( !queryOnly ) {
	  (void) changeGeometry(gb, myreply.width, myreply.height, False, NULL);
	  /* TODO: necessary? */
	  XtClass((Widget)gb)->core_class.resize((Widget)gb) ;
	  return XtGeometryDone ;
	}
	else
	  return XtGeometryYes ;
      }
    }

    else {
      /* Cell resize not allowed, but maybe the new request will fit within
       * the current cell size
       */
      computeCellSize(gb, gc, &cell_width, &cell_height) ;

      if( queryOnly )
      {
	/* put things back the way they were */
	gc->gridbox.prefWidth = old_cw ;
	gc->gridbox.prefHeight = old_ch ;
      }

      if( cell_width >= gc->gridbox.prefWidth &&
	  cell_height >= gc->gridbox.prefHeight )
      {
	if( !queryOnly ) {
	  (void) changeGeometry(gb, myreply.width, myreply.height, False, NULL);
	  XtClass((Widget)gb)->core_class.resize((Widget)gb) ;
	  return XtGeometryDone ;
	}
	else
	  return XtGeometryYes ;
      }
    }

    /* TODO: this needs to be re-worked.  If we offer a compromise,
     * it MUST be one that will be accepted on the next call.  Unfortunately,
     * it doesn't work that way because we always try to distribute
     * excess space to the children.  This can result in the next call
     * giving a different answer than this one did.
     */
#ifdef	COMMENT
    reply->width = cell_width - margin ;
    reply->height = cell_height - margin ;
    reply->request_mode = CWWidth | CWHeight ;
    return XtGeometryAlmost ;
#endif	/* COMMENT */
    return XtGeometryNo ;
}



/* No delete_child function.  It might make sense to resize the
 * grid again, but for now we'll just leave it alone.
 */




	/* CONSTRAINT WIDGET FUNCTIONS */


/* ARGSUSED */
static	void
GridboxConstraintInit(request, new, args, num_args)
    Widget request, new;
    ArgList args;
    Cardinal *num_args;
{
    GridboxConstraints gc = (GridboxConstraints)new->core.constraints;
    GridboxWidget gb = (GridboxWidget)new->core.parent;

    if( gc->gridbox.margin < 0 )
	gc->gridbox.margin = gb->gridbox.defaultDistance;

    /* TODO: how about resources that cause a child to use all
     * remaining space, or to start a new row?
     */
}



	/* No constraint destroy function */


/* ARGSUSED */
static	Boolean
GridboxConstraintSetValues(current, request, new, args, num_args)
    Widget current, request, new;
    ArgList args;
    Cardinal *num_args;
{
  GridboxConstraints gcCur = (GridboxConstraints) current->core.constraints;
  GridboxConstraints gcNew = (GridboxConstraints) new->core.constraints;
  GridboxWidget gb = (GridboxWidget) XtParent(new) ;

  if (gcCur->gridbox.gridx	!= gcNew->gridbox.gridx		||
      gcCur->gridbox.gridy	!= gcNew->gridbox.gridy		||
      gcCur->gridbox.gridWidth	!= gcNew->gridbox.gridWidth	||
      gcCur->gridbox.gridHeight != gcNew->gridbox.gridHeight )
  {
      freeAll(gb) ;
      getPreferredSizes(gb) ;
      gb->gridbox.needs_layout = True ;
  }

  else if( gcCur->gridbox.fill		!= gcNew->gridbox.fill		||
	   gcCur->gridbox.gravity	!= gcNew->gridbox.gravity	||
	   gcCur->gridbox.gravity	!= gcNew->gridbox.gravity	||
	   gcCur->gridbox.gravity	!= gcNew->gridbox.gravity	||
	   gcCur->gridbox.gravity	!= gcNew->gridbox.gravity )
  {
      gb->gridbox.needs_layout = True ;
  }

  return False ;		/* what does this signify? */
}





	/* PRIVATE ROUTINES */



	/* Query all children, find out how much space they want.
	 * Add some for border & margin.
	 * Call this whenever the set of managed children changes.
	 */

static	void
getPreferredSizes(gb)
	GridboxWidget	gb ;
{
	int	i ;
	Widget	*childP ;
	int	margin ;
	GridboxConstraints gc, prevc=NULL ;
	XtWidgetGeometry	preferred ;

	for( i=0, childP = gb->composite.children;
	     i < gb->composite.num_children ;
	     ++i, ++childP )
	  if( XtIsManaged(*childP) )
	  {
	    gc = (GridboxConstraints) (*childP)->core.constraints ;

	    if( gc->gridbox.allowResize )
	      (void) XtQueryGeometry(*childP, NULL, &preferred) ;
	    else {
	      preferred.width = (*childP)->core.width ;
	      preferred.height = (*childP)->core.height ;
	      preferred.border_width = (*childP)->core.border_width ;
	    }
	    margin = (gc->gridbox.margin + preferred.border_width) * 2 ;
	    gc->gridbox.prefWidth = preferred.width + margin ;
	    gc->gridbox.prefHeight = preferred.height + margin ;

	    if( gc->gridbox.gridx == GRIDBOX_NEXT )
	      gc->gridbox.gridx = prevc == NULL ? 0 :
		      prevc->gridbox.gridx + prevc->gridbox.gridWidth ;

	    else if( gc->gridbox.gridx == GRIDBOX_SAME )
	      gc->gridbox.gridx = prevc == NULL ? 0 : prevc->gridbox.gridx ;

	    if( gc->gridbox.gridy == GRIDBOX_NEXT )
	      gc->gridbox.gridy = prevc == NULL ? 0 :
		      prevc->gridbox.gridy + prevc->gridbox.gridHeight ;

	    else if( gc->gridbox.gridy == GRIDBOX_SAME )
	      gc->gridbox.gridy = prevc == NULL ? 0 : prevc->gridbox.gridy ;

	    prevc = gc ;
	  }
}


	/* Given a gridbox & child, compute the current size of
	 * the cell occupied by the child.
	 */

static	void
computeCellSize(gb, gc, rwid,rhgt)
	GridboxWidget	gb ;
	GridboxConstraints gc ;
	Dimension	*rwid, *rhgt ;
{
	Position	x,y ;
	Dimension	wid,hgt ;
	int		i ;
	Dimension	*wids, *hgts ;

	wids = gb->gridbox.wids ;
	hgts = gb->gridbox.hgts ;

	x = gc->gridbox.gridx ;
	y = gc->gridbox.gridy ;
	for(wid=0, i=0; i < gc->gridbox.gridWidth; ++i)
	  wid += wids[x+i] ;
	for(hgt=0, i=0; i < gc->gridbox.gridHeight; ++i)
	  hgt += hgts[y+i] ;

	*rwid = wid ;
	*rhgt = hgt ;
}



static	void
allocAll(gb)
    GridboxWidget	gb ;
{
    gb->gridbox.max_wids	= XTCALLOC(gb->gridbox.nx, Dimension) ;
    gb->gridbox.max_hgts	= XTCALLOC(gb->gridbox.ny, Dimension) ;
    gb->gridbox.wids		= XTCALLOC(gb->gridbox.nx, Dimension) ;
    gb->gridbox.hgts		= XTCALLOC(gb->gridbox.ny, Dimension) ;
    gb->gridbox.max_weightx	= XTCALLOC(gb->gridbox.nx, int) ;
    gb->gridbox.max_weighty	= XTCALLOC(gb->gridbox.ny, int) ;
}

static	void
freeAll(gb)
    GridboxWidget	gb ;
{
    XtFree((char *)gb->gridbox.max_wids) ;	gb->gridbox.max_wids = NULL ;
    XtFree((char *)gb->gridbox.max_hgts) ;	gb->gridbox.max_hgts = NULL ;
    XtFree((char *)gb->gridbox.wids) ;		gb->gridbox.wids = NULL ;
    XtFree((char *)gb->gridbox.hgts) ;		gb->gridbox.hgts = NULL ;
    XtFree((char *)gb->gridbox.max_weightx) ;	gb->gridbox.max_weightx = NULL;
    XtFree((char *)gb->gridbox.max_weighty) ;	gb->gridbox.max_weighty = NULL;
}




	/* This function and the ones that follow are the meat
	 * of the gridbox widget.  They perform the following actions:
	 *
	 * 1) loop through all children, finding their preferred
	 *    sizes.  (This has already been done in getPreferredSizes.)
	 *
	 * 2) Determine how many rows & columns there are in the grid.
	 *
	 * 3) Compute desired sizes for all rows & columns.  See below.
	 *
	 * 4) Compute sums of row & column sizes.  This is our own
	 *    preferred size.  See below.
	 */

static	void
computeWidHgtInfo(gb)
    GridboxWidget	gb ;
{
    Widget	*childP ;
    int		i ;
    int		nc=0, nr=0 ;
    int		maxgw=0, maxgh=0 ;	/* max size in cells */
    GridboxConstraints	gc ;

    if( gb->composite.num_children <= 0 )
      return ;

    freeAll(gb) ;		/* start with clean slate */

    /* step 2:	Find out how many rows & columns there will be.
     */

    for( i = gb->composite.num_children, childP = gb->composite.children;
	--i >= 0; ++childP)
      if( XtIsManaged(*childP) )
      {
	gc = (GridboxConstraints) (*childP)->core.constraints ;

	if( gc->gridbox.gridWidth > maxgw )
	  maxgw = gc->gridbox.gridWidth ;
	if( gc->gridbox.gridHeight > maxgh )
	  maxgh = gc->gridbox.gridHeight ;

	if( gc->gridbox.gridx + gc->gridbox.gridWidth > nc )
	  nc = gc->gridbox.gridx + gc->gridbox.gridWidth ;
	if( gc->gridbox.gridy + gc->gridbox.gridHeight > nr )
	  nr = gc->gridbox.gridy + gc->gridbox.gridHeight ;
      }

    gb->gridbox.nx = nc ;
    gb->gridbox.ny = nr ;
    gb->gridbox.maxgw = maxgw ;
    gb->gridbox.maxgh = maxgh ;

    allocAll(gb) ;


    /* step 3 & 4, examine children for the size they need,
     * compute row & column sizes accordingly.
     */

    computeWidHgtMax(gb) ;
}




	/*
	 * Compute desired sizes for all rows & columns:
	 *   a) for all single-celled children, set the max desired
	 *	size for the corresponding rows & columns.
	 *   b) for all two-column children, set the max desired
	 *	size for both of the corresponding columns
	 *	by distributing the excess proportionally.
	 *   c) repeat for two-row children.
	 *   d) repeat for three-column, children
	 *   e) repeat for three-row children.
	 *   f) etc., until all children have been accounted for.
	 *
	 *   This is a non-deterministic algorithm, i.e. it is not
	 *   guaranteed to find the optimum row & column sizes.
	 *   I will have to give this some more thought.
	 *
	 * Compute sums of row & column sizes.	This is our own
	 *    preferred size.
	 */

static	void
computeWidHgtMax(gb)
    GridboxWidget	gb ;
{
    Widget	*childP ;
    int		i,j ;
    int		nc=0, nr=0 ;
    Dimension	*wids, *hgts ;
    int		*weightx, *weighty ;
    int		maxgw=0, maxgh=0 ;	/* max size in cells */
    GridboxConstraints	gc ;

    if( gb->composite.num_children <= 0 )
      return ;

    nc = gb->gridbox.nx ;
    nr = gb->gridbox.ny ;
    maxgw = gb->gridbox.maxgw ;
    maxgh = gb->gridbox.maxgh ;

    wids = gb->gridbox.max_wids ;
    hgts = gb->gridbox.max_hgts ;
    weightx = gb->gridbox.max_weightx ;
    weighty = gb->gridbox.max_weighty ;

    /* step 3, examine children for the size they need,
     * compute row & column sizes accordingly.
     *
     * This is not coded effeciently, and might benefit from some
     * rethinking if the Gridbox widget is to be used with large grids.
     *
     * This also generates a non-optimum answer if large cells
     * partially overlap.
     */

    /* Column widths */
    memset(wids, 0, nc * sizeof(Dimension)) ;
    for(j=1; j<=maxgw; ++j)
    {
      for( i=0, childP = gb->composite.children;
	   i < gb->composite.num_children ;
	   ++i, ++childP )
	if( XtIsManaged(*childP) )
	{
	  gc = (GridboxConstraints) (*childP)->core.constraints ;
	  if( gc->gridbox.gridWidth == j )
	    computeWidHgtUtil(gc->gridbox.gridx, gc->gridbox.gridWidth,
		  gc->gridbox.prefWidth, gc->gridbox.weightx, wids, weightx) ;
	}
    }

    /* column heights */
    memset(hgts, 0, nr * sizeof(Dimension)) ;
    for(j=1; j<=maxgh; ++j)
    {
      for( i=0, childP = gb->composite.children;
	   i < gb->composite.num_children ;
	   ++i, ++childP )
	if( XtIsManaged(*childP) )
	{
	  gc = (GridboxConstraints) (*childP)->core.constraints ;
	  if( gc->gridbox.gridHeight == j )
	    computeWidHgtUtil(gc->gridbox.gridy, gc->gridbox.gridHeight,
		  gc->gridbox.prefHeight, gc->gridbox.weighty, hgts, weighty) ;
	}
    }


    /* Step 4: compute sums */

    gb->gridbox.total_wid = 0 ;
    gb->gridbox.total_weightx = 0 ;
    for(i=0; i<gb->gridbox.nx; ++i) {
      gb->gridbox.total_wid += wids[i] ;
      gb->gridbox.total_weightx += weightx[i] ;
    }

    gb->gridbox.total_hgt = 0 ;
    gb->gridbox.total_weighty = 0 ;
    for(i=0; i<gb->gridbox.ny; ++i) {
      gb->gridbox.total_hgt += hgts[i] ;
      gb->gridbox.total_weighty += weighty[i] ;
    }
}



static	void
computeWidHgtUtil(idx, ncell, wid, weight, wids, weights)
    int			idx, ncell ;
    int			wid, weight ;
    Dimension		*wids ;
    int			*weights ;
{
    /* 1 set the specified column weight(s) to the max of their current
     *	 value and the weight of this widget.
     *
     * 2 find out if the available space in the indicated column(s)
     *	 is enough to satisfy this widget.  If not, distribute the
     *	 excess size by column weights.
     *
     *   The excess may not divide evenly into the number of cells.
     *   The remainder will also be distributed evenly to some of the
     *   cells.  Make a Bresenham walk to do this.
     */

    int		i, cwid = 0 ;
    int		wtot = 0 ;		/* total weight of cells */
    int		excess ;
    int		rem, count ;		/* bresenham */

    if( ncell == 1 )		/* simple case */
    {
      if( weights[idx] < weight ) weights[idx] = weight ;
      if( wids[idx] < wid ) wids[idx] = wid ;
      return ;
    }


    for(i=0; i<ncell; ++i)	/* multi-cell cases */
    {
      assert(idx+i >= 0) ;
      if( weights[idx+i] < weight )
	weights[idx+i] = weight ;
      cwid += wids[idx+i] ;
      wtot += weights[idx+i] ;
    }

    if( cwid < wid )		/* need to increase cell size(s) */
    {
      excess = wid - cwid ;
      if( ncell == 1 )		/* one cell: assign excess to the cell */
	wids[idx] = wid ;

      else if( wtot == 0 )	/* weights all zero, distribute evenly */
      {
	rem = excess % ncell ;
	count = (ncell-rem)/2 ;
	excess /= ncell ;
	for(i=0; i<ncell; ++i)
	{
	  wids[idx+i] += excess ;
	  if( (count -= rem) < 0 ) {
	    ++wids[idx+i] ;
	    count += ncell ;
	  }
	}
      }

      else			/* weighted */
      {
	rem = (excess*wtot) % ncell ;
	count = (ncell-rem)/2 ;
	for(i=0; i<ncell; ++i)
	{
	  assert(idx+i >= 0) ;
	  wids[idx+i] += excess*weights[idx+i]/wtot ;
	  if( (count -= rem) < 0 ) {
	    ++wids[idx+i] ;
	    count += ncell ;
	  }
	}
      }
    }
}



	/* Layout function.  Given a width & height, determine
	 * sizes of all the rows & columns
	 */

static	void
layout(gb, width, height)
    GridboxWidget gb ;
    int		width, height ;
{
    int		i,j ;
    int		excess ;
    int		weight ;
    Dimension	*wids, *hgts ;
    int		mincellsize = gb->gridbox.defaultDistance * 2 + 1 ;

    if( gb->gridbox.nx <= 0 || gb->gridbox.ny <= 0 )
      return ;

    /* find out how much excess there is */

    wids =  gb->gridbox.wids ;
    memcpy(wids, gb->gridbox.max_wids, gb->gridbox.nx * sizeof(Dimension)) ;
    excess = width - gb->gridbox.total_wid ;
    weight = gb->gridbox.total_weightx ;

    /* distribute excess to the columns & assign positions */
    if( weight > 0 )
      for(i=0; i < gb->gridbox.nx; ++i)
	if( gb->gridbox.max_weightx[i] > 0 )
	{
	  j = wids[i] + gb->gridbox.max_weightx[i]*excess/weight ;
	  wids[i] = max(j,mincellsize) ;
	}

    /* Same again, for heights */
    hgts = gb->gridbox.hgts ;
    memcpy(hgts, gb->gridbox.max_hgts, gb->gridbox.ny * sizeof(Dimension)) ;
    excess = height - gb->gridbox.total_hgt ;
    weight = gb->gridbox.total_weighty ;

    /* distribute it to the rows */
    if( weight > 0 )
      for(i=0; i < gb->gridbox.ny; ++i)
	if( gb->gridbox.max_weighty[i] > 0 )
	{
	  j = hgts[i] + gb->gridbox.max_weighty[i]*excess/weight ;
	  hgts[i] = max(j,mincellsize) ;
	}
}



	/* Given a gridbox & child, compute the size and placement of
	 * the child within the cell.
	 */

static	void
layoutChild(gb, w, rwid,rhgt, rx,ry)
    GridboxWidget	gb ;
    Widget		w ;
    Dimension		*rwid, *rhgt ;
    Position		*rx, *ry ;
{
    GridboxConstraints gc = (GridboxConstraints)w->core.constraints;
    Dimension	width, height;
    int		margin ;
    int		excess ;

    if( !XtIsManaged(w) )
      return ;

    computeCellSize(gb, gc, &width,&height) ;

    /* Correct for preferred fill & alignment */
    if( !(gc->gridbox.fill & FillWidth)	 &&
	(excess = width - gc->gridbox.prefWidth) > 0 )
    {
      switch( gc->gridbox.gravity ) {
	case CenterGravity: case NorthGravity: case SouthGravity:
	  *rx += excess/2 ;
	  break ;
	case EastGravity: case NorthEastGravity: case SouthEastGravity:
	  *rx += excess ;
	  break ;
      }
      width = gc->gridbox.prefWidth ;
    }

    if( !(gc->gridbox.fill & FillHeight)  &&
	(excess = height - gc->gridbox.prefHeight) > 0 )
    {
      switch( gc->gridbox.gravity ) {
	case CenterGravity: case WestGravity: case EastGravity:
	  *ry += excess/2 ;
	  break ;
	case SouthGravity: case SouthWestGravity: case SouthEastGravity:
	  *ry += excess ;
	  break ;
      }
      height = gc->gridbox.prefHeight ;
    }

    margin = gc->gridbox.margin ;

    width -= 2 * w->core.border_width + 2 * margin ;
    height -= 2 * w->core.border_width + 2 * margin ;
    *rwid = max(width,1) ;
    *rhgt = max(height,1) ;
}




	/* Make size change request.  Always return the resulting size.	 */

static	XtGeometryResult
changeGeometry(gb, req_width,req_height, queryOnly, reply)
    GridboxWidget	gb ;
    int			req_width, req_height ;
    int			queryOnly ;
    XtWidgetGeometry	*reply ;
{
    XtGeometryResult	result ;
    Dimension	old_width = gb->core.width, old_height = gb->core.height;

    if( req_width != gb->core.width  ||	 req_height != gb->core.height )
    {
      XtWidgetGeometry myrequest ;

      myrequest.width = req_width ;
      myrequest.height = req_height ;
      myrequest.request_mode = CWWidth | CWHeight ;
      if( queryOnly )
	myrequest.request_mode |= XtCWQueryOnly ;

      result = XtMakeGeometryRequest((Widget)gb, &myrequest, reply) ;

      /* BUG.  The Athena box widget (and probably others) will change
       * our dimensions even if this is only a query.  To work around that
       * bug, we restore our dimensions after such a query.
       */
      if( queryOnly ) {
	gb->core.width = old_width ;
	gb->core.height = old_height ;
      }
    }
    else
      result = XtGeometryNo ;

    if( reply != NULL )
      switch( result ) {
	case XtGeometryYes:
	case XtGeometryDone:
	  reply->width = req_width ;
	  reply->height = req_height ;
	  break ;
	case XtGeometryNo:
	  reply->width = old_width ;
	  reply->height = old_height ;
	  break ;
	case XtGeometryAlmost:
	  break ;
      }

    return result ;
}





	/* RESOURCES */


#define done(type, value)			\
  {						\
    if( toVal->addr != NULL ) {			\
      if( toVal->size < sizeof(type)) {		\
	toVal->size = sizeof(type) ;		\
	return False ;				\
      }						\
      *(type*)(toVal->addr) = (value);		\
    }						\
    else					\
      toVal->addr = (XtPointer)&(value);	\
    toVal->size = sizeof(type) ;		\
    return True ;				\
  }



/* ARGSUSED */
static	Boolean
_CvtStringToFillType(dpy, args, num_args, fromVal, toVal, data)
    Display	*dpy ;
    XrmValuePtr args;		/* unused */
    Cardinal	*num_args;	/* unused */
    XrmValuePtr fromVal;
    XrmValuePtr toVal;
    XtPointer	*data ;
{
    String	str = (String)fromVal->addr ;
    static int	fillType;

    if( XmuCompareISOLatin1(str, "none") == 0  ||
	XmuCompareISOLatin1(str, "fillnone") == 0 )
      fillType = FillNone ;
    else if( XmuCompareISOLatin1(str, "width") == 0  ||
	     XmuCompareISOLatin1(str, "fillwidth") == 0	 ||
	     XmuCompareISOLatin1(str, "horizontal") == 0  ||
	     XmuCompareISOLatin1(str, "x") == 0 )
      fillType = FillWidth ;
    else if( XmuCompareISOLatin1(str, "height") == 0  ||
	     XmuCompareISOLatin1(str, "fillheight") == 0  ||
	     XmuCompareISOLatin1(str, "vertical") == 0	||
	     XmuCompareISOLatin1(str, "y") == 0 )
      fillType = FillHeight ;
    else if( XmuCompareISOLatin1(str, "both") == 0  ||
	     XmuCompareISOLatin1(str, "fillboth") == 0	||
	     XmuCompareISOLatin1(str, "all") == 0  ||
	     XmuCompareISOLatin1(str, "xy") == 0 )
      fillType = FillBoth ;
    else {
      XtStringConversionWarning(fromVal->addr, XtRFillType);
      return False ;
    }

    done(int, fillType) ;
}


/* ARGSUSED */
static	Boolean
_CvtStringToGridPosition(dpy, args, num_args, fromVal, toVal, data)
    Display	*dpy ;
    XrmValuePtr args;		/* unused */
    Cardinal	*num_args;	/* unused */
    XrmValuePtr fromVal;
    XrmValuePtr toVal;
    XtPointer	*data ;
{
    String	str = (String)fromVal->addr ;
    static Position position ;

    if( XmuCompareISOLatin1(str, "gridboxnext") == 0  ||
	XmuCompareISOLatin1(str, "gridnext") == 0  ||
	XmuCompareISOLatin1(str, "next") == 0 )
      position = GRIDBOX_NEXT ;
    else if( XmuCompareISOLatin1(str, "gridboxsame") == 0  ||
	     XmuCompareISOLatin1(str, "gridsame") == 0 ||
	     XmuCompareISOLatin1(str, "same") == 0 )
      position = GRIDBOX_SAME ;
    else
      return XtCvtStringToShort(dpy, args,num_args, fromVal, toVal, data) ;

    done(Position, position) ;
}
