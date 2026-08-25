var LastLink = null;
var LastDiv  = null;
var LastTop  = 0;
var LastUrl  = null;
function ShowFullSize(Link, DivLeft, ImgFile)
{
    if (DivLeft < 20)
        DivLeft = 20;
    
    if (Link == LastLink)
    {
        HideFullSize(); // sets LastLink = null
        return;
    }

    HideFullSize();

    var Div = document.getElementById("DivFullSize");
    var Img = document.getElementById("ImgFullSize");
    var Scr = document.getElementById("DivScrollable");

    var Top = (window.pageYOffset || // best option, but not available on IE 8
               document.documentElement.scrollTop || // quirks mode
               document.body.scrollTop) + 15;        // strict mode

    Div.style.top  = Top + "px";
    Div.style.left = DivLeft + "px";
    Div.style.display = "block";

    // AFTER Div
    Scr.style.overflow = "auto";
    Scr.scrollLeft = 0;
    Scr.scrollTop  = 0;

    Link.innerHTML = "Hide Full Size";
    LastLink = Link;
    LastDiv  = Div;

    Img.style.maxWidth = "none";
    Img.onload = function()
    {
        var MaxWidth  = (window.innerWidth || // best option, but not available on IE 8
                         document.documentElement.clientWidth ||  // quirks mode
                         document.body.clientWidth)  - DivLeft - 60;        // strict mode

        var MaxHeight = (window.innerHeight || // best option, but not available on IE 8
                         document.documentElement.clientHeight || // quirks mode
                         document.body.clientHeight) - DivLeft - 60;        // strict mode

        var ImgWidth  = Img.width  + 4;
        var ImgHeight = Img.height + 4;

        var HScroll = (ImgWidth  > MaxWidth);
        var VScroll = (ImgHeight > MaxHeight);

        if (VScroll) ImgWidth  += 18;
        if (HScroll) ImgHeight += 18;

        if (HScroll) Scr.style.width  = MaxWidth  + "px";
        else         Scr.style.width  = ImgWidth  + "px";
        if (VScroll) Scr.style.height = MaxHeight + "px";
        else         Scr.style.height = ImgHeight + "px";
    }

    Img.src = ImgFile;
}
function HideFullSize()
{
    if (LastLink != null)
    {
        LastDiv.style.display = "none";
        LastLink.innerHTML = "Show Full Size";
        LastDiv  = null;
        LastLink = null;
    }
    ShowMenu(false);
}

function ShowMenu(bShow)
{
    var MenuHead = document.getElementById("MenuHead");
    var MenuCont = document.getElementById("MenuContent");

    if (MenuHead != null && MenuCont != null)
    {
        MenuHead.style.display = bShow ? "none"  : "block";
        MenuCont.style.display = bShow ? "block" : "none";
    }
}

function onScrollCallback(e)
{
    var Top = (window.pageYOffset || // best option, but not available on IE 8
               document.documentElement.scrollTop || // quirks mode
               document.body.scrollTop);             // strict mode

    if (Math.abs(LastTop - Top) > 50) // Workaround for buggy Firefox 79
    {
        if (LastUrl == window.location.href)
            HideFullSize(); // also hide menu

        LastTop = Top;
        LastUrl = window.location.href;
    }
}

if (window.addEventListener) // Modern browsers
    window.addEventListener('scroll', onScrollCallback, false);
else if (window.attachEvent) // Internet Explorer 8
    window.attachEvent('onscroll', onScrollCallback);
