package app.organicmaps.car.util;

import androidx.car.app.model.CarColor;

public final class Colors
{
  // Backseat Driver: the night-drive look. Caution yellow is the accent (the host picks readable text
  // on it), and the turn card sits on the dark card colour instead of CoMaps green.
  private static final int CARD = 0xFF1F2226;

  public static final CarColor DEFAULT = CarColor.DEFAULT;
  public static final CarColor DISTANCE = CarColor.YELLOW;
  public static final CarColor LOCATION_TINT = CarColor.YELLOW;
  public static final CarColor OPENING_HOURS_OPEN = CarColor.GREEN;
  public static final CarColor OPENING_HOURS_CLOSES_SOON = CarColor.YELLOW;
  public static final CarColor OPENING_HOURS_CLOSED = CarColor.RED;
  public static final CarColor START_NAVIGATION = CarColor.YELLOW;
  public static final CarColor NAVIGATION_TEMPLATE_BACKGROUND = CarColor.createCustom(CARD, CARD);
  public static final CarColor BUTTON_ACCEPT = CarColor.YELLOW;

  private Colors() {}
}
