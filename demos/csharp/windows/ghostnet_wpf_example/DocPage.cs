﻿using System;
using System.ComponentModel;
using System.Windows.Media.Imaging;
using System.Collections.ObjectModel;

namespace ghostnet_wpf_example
{
	public class DocPage : INotifyPropertyChanged
	{
		private int height;
		private int width;
		private double zoom;
		private bool aa;
		private BitmapSource bitmap;
		private String pagename;
		private int pagenum;
		private Page_Content_t content;

		public int Height
		{
			get { return height; }
			set
			{
				height = value;
				OnPropertyChanged("Height");
			}
		}

		public int Width
		{
			get { return width; }
			set
			{
				width = value;
				OnPropertyChanged("Width");
			}
		}

		public double Zoom
		{
			get { return zoom; }
			set { zoom = value; }
		}

		public bool AA
		{
			get { return aa; }
			set { aa = value; }
		}

		public BitmapSource BitMap
		{
			get { return bitmap; }
			set
			{
				bitmap = value;
				OnPropertyChanged("BitMap");
			}
		}

		public String PageName
		{
			get { return pagename; }
			set { pagename = value; }
		}

		public int PageNum
		{
			get { return pagenum; }
			set { pagenum = value; }
		}
		public Page_Content_t Content
		{
			get { return content; }
			set { content = value; }
		}

		public event PropertyChangedEventHandler PropertyChanged;

		// Create the OnPropertyChanged method to raise the event
		protected void OnPropertyChanged(string name)
		{
			PropertyChangedEventHandler handler = PropertyChanged;
			if (handler != null)
			{
				handler(this, new PropertyChangedEventArgs(name));
			}
		}

		public DocPage()
		{
			this.height = 0;
			this.width = 0;
			this.zoom = 0;
			this.bitmap = null;
			this.pagenum = -1;
			this.pagename = "";
		}

		public DocPage(int Height, int Width, double Zoom, BitmapSource BitMap, int PageNum, bool AA)
		{
			this.height = Height;
			this.width = Width;
			this.zoom = Zoom;
			this.bitmap = BitMap;
			this.aa = AA;
			this.pagename = ("Page " + (pagenum + 1));
		}
	};
	public class Pages : ObservableCollection<DocPage>
	{
		public Pages()
			: base()
		{
		}
	}
}
