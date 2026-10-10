// Mike OS: Amber - default desktop: one classic taskbar along the bottom.
var logo = "/usr/share/pixmaps/mikeos-amber.svg";

var panel = new Panel;
panel.location = "bottom";
panel.height = 32;

var menu = panel.addWidget("org.kde.plasma.kicker");
menu.currentConfigGroup = ["General"];
menu.writeConfig("icon", logo);
menu.writeConfig("useCustomButtonImage", true);
menu.writeConfig("customButtonImage", "file://" + logo);

panel.addWidget("org.kde.plasma.taskmanager");
panel.addWidget("org.kde.plasma.systemtray");
panel.addWidget("org.kde.plasma.digitalclock");

var desktops = desktopsForActivity(currentActivity());
for (var i = 0; i < desktops.length; i++) {
    var d = desktops[i];
    d.wallpaperPlugin = "org.kde.image";
    d.currentConfigGroup = ["Wallpaper", "org.kde.image", "General"];
    d.writeConfig("Image", "file:///usr/share/wallpapers/MikeOS-Amber/contents/images/1920x1080.png");
}
